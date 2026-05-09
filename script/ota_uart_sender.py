#!/usr/bin/env python3
"""
STM32F407 OTA UART sender for this project.

Protocol matches Component/ota/ota_protocol.h and Component/ota/ota_uart.c:
  - START / DATA / END frames
  - ACK / NACK replies
  - PAUSE / RESUME text flow control

Typical usage:
  python script/ota_uart_sender.py --port COM5 --bin build/cross/arm/release/app.bin
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("Missing dependency: pyserial")
    print("Install with: pip install pyserial")
    raise SystemExit(1)


MAGIC = 0x5AA5C33C

FRAME_HEADER_FMT = "<IBBHIHHI"
FRAME_HEADER_SIZE = struct.calcsize(FRAME_HEADER_FMT)

TYPE_START = 0x01
TYPE_DATA = 0x02
TYPE_END = 0x03
TYPE_ACK = 0x81
TYPE_NACK = 0x82

MAX_PAYLOAD = 512

DEFAULT_BAUD = 115200
DEFAULT_APP1_ADDR = 0x08020000
DEFAULT_IMAGE_TYPE = 1
DEFAULT_HW_ID = 0


def parse_int(text: str) -> int:
    return int(text, 0)


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def make_frame(frame_type: int, seq: int, offset: int, payload: bytes = b"", crc_override: int | None = None) -> bytes:
    crc = crc32(payload) if crc_override is None else (crc_override & 0xFFFFFFFF)
    header = struct.pack(
        FRAME_HEADER_FMT,
        MAGIC,
        frame_type,
        0,
        seq & 0xFFFF,
        offset & 0xFFFFFFFF,
        len(payload) & 0xFFFF,
        0,
        crc,
    )
    return header + payload


def make_start_payload_v1(version: int, app_size: int, app_crc32: int) -> bytes:
    return struct.pack("<IIII", MAGIC, version, app_size, app_crc32)


def make_start_payload_v2(version: int, app_size: int, app_crc32: int, target_addr: int, image_type: int, hw_id: int) -> bytes:
    header_size = struct.calcsize("<IHHIIIIIII")
    body_wo_crc = struct.pack(
        "<IHHIIIIII",
        MAGIC,
        2,
        header_size,
        version,
        app_size,
        app_crc32,
        target_addr,
        image_type,
        hw_id,
    )
    header_crc = crc32(body_wo_crc)
    return body_wo_crc + struct.pack("<I", header_crc)


class ReplyTimeout(RuntimeError):
    pass


class Sender:
    def __init__(self, ser: serial.Serial, args: argparse.Namespace) -> None:
        self.ser = ser
        self.args = args
        self.rx_buf = bytearray()
        self.text_buf = ""
        self.flow_paused = False
        self.magic_bytes = struct.pack("<I", MAGIC)

    def _handle_text(self, raw: bytes) -> None:
        text = raw.decode("utf-8", errors="ignore")
        if not text:
            return

        self.text_buf += text.replace("\r", "\n")
        lines = self.text_buf.split("\n")
        self.text_buf = lines.pop()

        for line in lines:
            line = line.strip()
            if not line:
                continue

            if "PAUSE" in line:
                self.flow_paused = True
                print("device: PAUSE")
                continue
            if "RESUME" in line:
                self.flow_paused = False
                print("device: RESUME")
                continue

            # The OTA port normally should not emit debug logs, but keep this
            # for visibility if future firmware adds text replies here.
            print(f"device: {line}")

    def _consume_text_prefix(self) -> None:
        while self.rx_buf:
            pos = self.rx_buf.find(self.magic_bytes)
            if pos < 0:
                # No frame header found: treat all as text.
                self._handle_text(bytes(self.rx_buf))
                self.rx_buf.clear()
                return
            if pos == 0:
                return

            self._handle_text(bytes(self.rx_buf[:pos]))
            del self.rx_buf[:pos]

    def _read_available(self) -> None:
        n = self.ser.in_waiting
        if n:
            self.rx_buf.extend(self.ser.read(n))
        self._consume_text_prefix()

    def _wait_flow_resume(self, timeout_s: float) -> None:
        deadline = time.time() + timeout_s
        while self.flow_paused:
            self._read_available()
            if not self.flow_paused:
                return
            if time.time() >= deadline:
                raise ReplyTimeout("flow control pause timeout")
            time.sleep(0.01)

    def _wait_reply(self, expect_seq: int, timeout_s: float) -> tuple[bool | None, int | None, int | None]:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            self._read_available()

            while len(self.rx_buf) >= FRAME_HEADER_SIZE:
                pos = self.rx_buf.find(self.magic_bytes)
                if pos < 0:
                    self._consume_text_prefix()
                    break
                if pos > 0:
                    self._handle_text(bytes(self.rx_buf[:pos]))
                    del self.rx_buf[:pos]
                    continue

                magic, frame_type, status, seq, offset, length, _reserved, payload_crc = struct.unpack(
                    FRAME_HEADER_FMT, bytes(self.rx_buf[:FRAME_HEADER_SIZE])
                )
                if magic != MAGIC:
                    del self.rx_buf[0]
                    continue

                if length != 0:
                    if len(self.rx_buf) < FRAME_HEADER_SIZE + length:
                        break
                    # We currently only expect zero-length ACK/NACK replies.
                    del self.rx_buf[: FRAME_HEADER_SIZE + length]
                    continue

                del self.rx_buf[:FRAME_HEADER_SIZE]

                if seq != (expect_seq & 0xFFFF):
                    continue

                if frame_type == TYPE_ACK:
                    return True, status, offset
                if frame_type == TYPE_NACK:
                    return False, status, offset

            time.sleep(0.01)

        return None, None, None

    def send_with_ack(self, frame: bytes, seq: int, label: str, timeout_s: float) -> int:
        tries = max(self.args.retries, 0) + 1
        for attempt in range(1, tries + 1):
            self._wait_flow_resume(max(timeout_s, 0.2))
            self.ser.write(frame)
            self.ser.flush()

            ok, status, offset = self._wait_reply(seq, timeout_s)
            if ok is True:
                return offset if offset is not None else 0
            if ok is False:
                print(f"{label}: NACK reason={status} device_offset={offset} retry={attempt}/{tries}")
            else:
                print(f"{label}: ACK timeout retry={attempt}/{tries}")

        raise ReplyTimeout(f"{label}: failed after retries")


def build_argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="STM32 UART OTA sender")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Baud rate (default: {DEFAULT_BAUD})")
    parser.add_argument("--bin", required=True, help="Firmware binary path")
    parser.add_argument("--version", type=parse_int, default=0x00010000, help="App version, supports 0x...")
    parser.add_argument("--header-version", choices=("v1", "v2"), default="v2", help="START payload format")
    parser.add_argument("--target-addr", type=parse_int, default=DEFAULT_APP1_ADDR, help="APP1 target address for v2 header")
    parser.add_argument("--image-type", type=parse_int, default=DEFAULT_IMAGE_TYPE, help="Image type for v2 header")
    parser.add_argument("--hw-id", type=parse_int, default=DEFAULT_HW_ID, help="Hardware ID for v2 header")
    parser.add_argument("--chunk", type=int, default=256, help=f"Payload bytes per DATA frame (1-{MAX_PAYLOAD})")
    parser.add_argument("--ack-timeout", type=float, default=2.0, help="ACK timeout per DATA/END frame (s)")
    parser.add_argument("--start-timeout", type=float, default=10.0, help="ACK timeout for START frame (s)")
    parser.add_argument("--retries", type=int, default=8, help="Retry count per frame")
    parser.add_argument("--tail-wait", type=float, default=1.5, help="Wait after END ACK before exit (s)")
    parser.add_argument("--write-timeout", type=float, default=1.0, help="Serial write timeout (s)")
    return parser


def main() -> int:
    parser = build_argparser()
    args = parser.parse_args()

    bin_path = Path(args.bin)
    if not bin_path.is_file():
        print(f"firmware not found: {bin_path}")
        return 1

    payload = bin_path.read_bytes()
    if not payload:
        print("firmware is empty")
        return 1

    app_size = len(payload)
    app_crc32 = crc32(payload)
    chunk = max(1, min(args.chunk, MAX_PAYLOAD))

    if args.header_version == "v2":
        start_payload = make_start_payload_v2(
            args.version,
            app_size,
            app_crc32,
            args.target_addr,
            args.image_type,
            args.hw_id,
        )
    else:
        start_payload = make_start_payload_v1(args.version, app_size, app_crc32)

    print(f"port      : {args.port}")
    print(f"baud      : {args.baud}")
    print(f"firmware  : {bin_path}")
    print(f"version   : 0x{args.version:08X}")
    print(f"size      : {app_size} bytes")
    print(f"crc32     : 0x{app_crc32:08X}")
    print(f"header    : {args.header_version}")
    print(f"target    : 0x{args.target_addr:08X}")
    print(f"chunk     : {chunk} bytes")

    try:
        with serial.Serial(args.port, args.baud, timeout=0.05, write_timeout=args.write_timeout) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            time.sleep(0.05)

            sender = Sender(ser, args)

            sender.send_with_ack(
                make_frame(TYPE_START, 0, 0, start_payload),
                0,
                "START",
                args.start_timeout,
            )
            print("start ack : received")

            sent = 0
            seq = 1
            while sent < app_size:
                payload_chunk = payload[sent : sent + chunk]
                ack_offset = sender.send_with_ack(
                    make_frame(TYPE_DATA, seq, sent, payload_chunk),
                    seq,
                    f"DATA seq={seq} off={sent}",
                    args.ack_timeout,
                )

                expected_offset = sent + len(payload_chunk)
                if ack_offset != expected_offset:
                    print(f"bad ACK offset: got {ack_offset}, expected {expected_offset}")
                    return 3

                sent = expected_offset
                seq = (seq + 1) & 0xFFFF

                if (sent % 4096 == 0) or (sent == app_size):
                    percent = sent * 100 // app_size
                    print(f"sent      : {sent}/{app_size} ({percent}%)")

            sender.send_with_ack(
                make_frame(TYPE_END, seq, app_size, b"", app_crc32),
                seq,
                "END",
                args.ack_timeout,
            )

            print("end ack   : received")
            print("device    : should reset to bootloader and process pending update")
            time.sleep(max(args.tail_wait, 0.0))

    except serial.SerialTimeoutException:
        print("serial write timeout")
        return 4
    except ReplyTimeout as exc:
        print(str(exc))
        return 5
    except serial.SerialException as exc:
        print(f"serial error: {exc}")
        return 6

    print("finished")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
