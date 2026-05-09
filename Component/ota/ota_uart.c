#include "ota_uart.h"
#include "ota_protocol.h"
#include "ota_crc32.h"
#include "ring_buffer.h"
#include "bl_partition.h"
#include "bl_param.h"
#include "bl_flash_if.h"
#include "app_uart_dma.h"
#include "debug_uart.h"
#include "main.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define OTA_RING_BUFFER_SIZE        (16U * 1024U)
#define OTA_FLOW_CTRL_PAUSE_LEVEL   (OTA_RING_BUFFER_SIZE * 80U / 100U)
#define OTA_FLOW_CTRL_RESUME_LEVEL  (OTA_RING_BUFFER_SIZE * 20U / 100U)

typedef enum
{
    OTA_STATE_WAIT_HEADER = 0,
    OTA_STATE_RECV_PAYLOAD
} ota_state_t;

typedef struct
{
    ota_state_t state;
    uint32_t app_version;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t recv_size;
    uint32_t crc_acc;
    uint16_t expected_seq;
    bool flow_paused;
} ota_context_t;

static ota_context_t s_ota_ctx = {
    OTA_STATE_WAIT_HEADER,
    0U,
    0U,
    0U,
    0U,
    0U,
    0U,
    false
};

/*
 * The STM32F407 parameter region uses a whole sector. We mirror the complete
 * sector in RAM, patch main/backup structures, then erase+rewrite the sector.
 */
static uint8_t s_param_page_cache[BL_PARAM_PAGE_SIZE];
static uint8_t s_ring_buffer_storage[OTA_RING_BUFFER_SIZE];
static ring_buffer_t s_ring_buffer;

static void ota_uart_send_bytes(const uint8_t *data, uint32_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }

    app_uart_dma_write(data, (uint16_t)len);
}

static void ota_uart_send_text(const char *text)
{
    if (text == NULL) {
        return;
    }

    ota_uart_send_bytes((const uint8_t *)text, (uint32_t)strlen(text));
}

static void ota_send_reply(uint8_t type, uint8_t status, uint16_t seq, uint32_t offset)
{
    ota_frame_header_t reply;

    memset(&reply, 0, sizeof(reply));
    reply.magic = OTA_FRAME_MAGIC;
    reply.type = type;
    reply.status = status;
    reply.seq = seq;
    reply.offset = offset;
    ota_uart_send_bytes((const uint8_t *)&reply, (uint32_t)sizeof(reply));
}

static void ota_send_ack(uint16_t seq, uint32_t offset)
{
    ota_send_reply(OTA_FRAME_TYPE_ACK, 0U, seq, offset);
}

static void ota_send_nack(uint16_t seq, uint32_t offset, uint8_t reason)
{
    ota_send_reply(OTA_FRAME_TYPE_NACK, reason, seq, offset);
}

static uint32_t param_calc_crc(const bl_param_t *param)
{
    return ota_crc32_calc((const uint8_t *)param, (uint32_t)offsetof(bl_param_t, param_crc32));
}

static void param_set_default(bl_param_t *param)
{
    memset(param, 0, sizeof(*param));
    param->magic = BL_PARAM_MAGIC;
    param->version = BL_PARAM_VERSION;
    param->update_flag = BL_UPDATE_FLAG_IDLE;
    param->app1_addr = BL_APP1_START_ADDR;
    param->app2_addr = BL_APP2_START_ADDR;
    param->tail_magic = BL_PARAM_TAIL_MAGIC;
    param->param_crc32 = param_calc_crc(param);
}

static bool param_is_min_valid(const bl_param_t *param)
{
    if (param == NULL) {
        return false;
    }

    return (param->magic == BL_PARAM_MAGIC) &&
           (param->tail_magic == BL_PARAM_TAIL_MAGIC) &&
           (param->version == BL_PARAM_VERSION);
}

static bool param_commit_update(uint32_t app_size, uint32_t app_crc32)
{
    bl_param_t main_param;
    bl_param_t backup_param;

    if ((app_size == 0U) || (app_size > BL_APP1_SIZE) || (app_size > BL_APP2_SIZE)) {
        return false;
    }

    memcpy(s_param_page_cache, (const void *)BL_PARAM_PAGE_ADDR, BL_PARAM_PAGE_SIZE);
    memcpy(&main_param, (const void *)BL_PARAM_MAIN_ADDR, sizeof(main_param));
    memcpy(&backup_param, (const void *)BL_PARAM_BACKUP_ADDR, sizeof(backup_param));

    if (!param_is_min_valid(&main_param)) {
        if (param_is_min_valid(&backup_param)) {
            main_param = backup_param;
        } else {
            param_set_default(&main_param);
        }
    }

    main_param.app_size = app_size;
    main_param.app_crc32 = app_crc32;
    main_param.app1_addr = BL_APP1_START_ADDR;
    main_param.app2_addr = BL_APP2_START_ADDR;
    main_param.update_flag = BL_UPDATE_FLAG_PENDING;
    main_param.last_error = BL_ERR_NONE;
    main_param.param_crc32 = param_calc_crc(&main_param);
    backup_param = main_param;

    memcpy(&s_param_page_cache[BL_PARAM_MAIN_ADDR - BL_PARAM_PAGE_ADDR], &main_param, sizeof(main_param));
    memcpy(&s_param_page_cache[BL_PARAM_BACKUP_ADDR - BL_PARAM_PAGE_ADDR], &backup_param, sizeof(backup_param));

    return bl_flash_program_param(s_param_page_cache, BL_PARAM_PAGE_SIZE);
}

void ota_uart_reset_state(void)
{
    memset(&s_ota_ctx, 0, sizeof(s_ota_ctx));
    s_ota_ctx.state = OTA_STATE_WAIT_HEADER;
    s_ota_ctx.crc_acc = 0xFFFFFFFFUL;
    s_ota_ctx.expected_seq = 0U;
    s_ota_ctx.flow_paused = false;
    ring_buffer_init(&s_ring_buffer, s_ring_buffer_storage, OTA_RING_BUFFER_SIZE);
}

static void ota_check_flow_control(void)
{
    uint32_t used = ring_buffer_available(&s_ring_buffer);

    if ((!s_ota_ctx.flow_paused) && (used >= OTA_FLOW_CTRL_PAUSE_LEVEL)) {
        s_ota_ctx.flow_paused = true;
        ota_uart_send_text("PAUSE\r\n");
    } else if (s_ota_ctx.flow_paused && (used <= OTA_FLOW_CTRL_RESUME_LEVEL)) {
        s_ota_ctx.flow_paused = false;
        ota_uart_send_text("RESUME\r\n");
    }
}

static bool ota_begin(const ota_header_t *header)
{
    uint32_t erase_size;

    if (header == NULL) {
        return false;
    }

    if ((header->app_size == 0U) ||
        (header->app_size > BL_APP1_SIZE) ||
        (header->app_size > BL_APP2_SIZE) ||
        (header->magic != OTA_FRAME_MAGIC)) {
        return false;
    }

    erase_size = (header->app_size + 3U) & ~3U;
    if (!bl_flash_erase(BL_APP2_START_ADDR, erase_size)) {
        my_printf("OTA: erase APP2 failed\r\n");
        return false;
    }

    s_ota_ctx.state = OTA_STATE_RECV_PAYLOAD;
    s_ota_ctx.app_version = header->app_version;
    s_ota_ctx.expected_size = header->app_size;
    s_ota_ctx.expected_crc32 = header->app_crc32;
    s_ota_ctx.recv_size = 0U;
    s_ota_ctx.crc_acc = 0xFFFFFFFFUL;
    s_ota_ctx.expected_seq = 1U;

    my_printf("OTA: begin ver=0x%08lX size=%lu crc=0x%08lX\r\n",
              s_ota_ctx.app_version, s_ota_ctx.expected_size, s_ota_ctx.expected_crc32);

    return true;
}

static bool ota_write_payload(const uint8_t *payload, uint32_t len)
{
    uint32_t flash_addr;

    if ((payload == NULL) || (len == 0U)) {
        return true;
    }

    if ((s_ota_ctx.recv_size + len) > s_ota_ctx.expected_size) {
        my_printf("OTA: size overflow recv=%lu len=%lu expect=%lu\r\n",
                  s_ota_ctx.recv_size, len, s_ota_ctx.expected_size);
        return false;
    }

    flash_addr = BL_APP2_START_ADDR + s_ota_ctx.recv_size;
    if (!bl_flash_program(flash_addr, payload, len)) {
        my_printf("OTA: flash write failed @ 0x%08lX len=%lu\r\n", flash_addr, len);
        return false;
    }

    s_ota_ctx.crc_acc = ota_crc32_update(s_ota_ctx.crc_acc, payload, len);
    s_ota_ctx.recv_size += len;
    return true;
}

static bool ota_finalize_if_done(void)
{
    uint32_t crc_value;

    if (s_ota_ctx.recv_size != s_ota_ctx.expected_size) {
        return false;
    }

    crc_value = ota_crc32_finalize(s_ota_ctx.crc_acc);
    if (crc_value != s_ota_ctx.expected_crc32) {
        my_printf("OTA: final crc mismatch expect=0x%08lX calc=0x%08lX\r\n",
                  s_ota_ctx.expected_crc32, crc_value);
        return false;
    }

    if (!param_commit_update(s_ota_ctx.expected_size, s_ota_ctx.expected_crc32)) {
        my_printf("OTA: param commit failed\r\n");
        return false;
    }

    my_printf("OTA: pending update armed\r\n");
    return true;
}

static bool ota_parse_start_payload(const uint8_t *payload, uint16_t len, ota_header_t *header)
{
    ota_image_header_v2_t header_v2;
    uint32_t crc_expect;

    if ((payload == NULL) || (header == NULL)) {
        return false;
    }

    if (len == sizeof(ota_header_t)) {
        memcpy(header, payload, sizeof(*header));
        if ((header->magic != OTA_FRAME_MAGIC) ||
            (header->app_size == 0U) ||
            (header->app_size > BL_APP1_SIZE) ||
            (header->app_size > BL_APP2_SIZE)) {
            return false;
        }
        return true;
    }

    if (len != sizeof(ota_image_header_v2_t)) {
        return false;
    }

    memcpy(&header_v2, payload, sizeof(header_v2));
    if ((header_v2.magic != OTA_FRAME_MAGIC) ||
        (header_v2.header_version != OTA_IMAGE_HEADER_V2_VERSION) ||
        (header_v2.header_size != sizeof(ota_image_header_v2_t)) ||
        (header_v2.target_addr != BL_APP1_START_ADDR) ||
        (header_v2.image_type != OTA_IMAGE_TYPE_APP1) ||
        (header_v2.app_size == 0U) ||
        (header_v2.app_size > BL_APP1_SIZE) ||
        (header_v2.app_size > BL_APP2_SIZE)) {
        return false;
    }

    crc_expect = ota_crc32_calc(payload, (uint32_t)offsetof(ota_image_header_v2_t, header_crc32));
    if (crc_expect != header_v2.header_crc32) {
        return false;
    }

    header->magic = header_v2.magic;
    header->app_version = header_v2.app_version;
    header->app_size = header_v2.app_size;
    header->app_crc32 = header_v2.app_crc32;
    return true;
}

void ota_uart_process_frame(const uint8_t *data, uint32_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }

    if (!ring_buffer_write(&s_ring_buffer, data, len)) {
        my_printf("OTA: ring buffer overflow len=%lu\r\n", len);
        return;
    }

    ota_check_flow_control();
}

void ota_uart_task(void)
{
    ota_frame_header_t frame;
    ota_header_t ota_header;
    uint8_t payload[OTA_FRAME_MAX_PAYLOAD];
    uint32_t available;

    while (1) {
        available = ring_buffer_available(&s_ring_buffer);
        if (available < sizeof(ota_frame_header_t)) {
            return;
        }

        (void)ring_buffer_peek(&s_ring_buffer, (uint8_t *)&frame, sizeof(frame));
        if (frame.magic != OTA_FRAME_MAGIC) {
            ring_buffer_drop(&s_ring_buffer, 1U);
            continue;
        }

        if (frame.length > OTA_FRAME_MAX_PAYLOAD) {
            ring_buffer_drop(&s_ring_buffer, 1U);
            ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_BAD_LENGTH);
            continue;
        }

        if (available < ((uint32_t)sizeof(ota_frame_header_t) + frame.length)) {
            return;
        }

        ring_buffer_drop(&s_ring_buffer, (uint32_t)sizeof(ota_frame_header_t));
        if (frame.length > 0U) {
            (void)ring_buffer_read(&s_ring_buffer, payload, frame.length);
        }

        if ((frame.length > 0U) && (ota_crc32_calc(payload, frame.length) != frame.crc32)) {
            ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_BAD_CRC);
            continue;
        }

        if (frame.type == OTA_FRAME_TYPE_START) {
            if ((frame.seq != 0U) || (frame.offset != 0U) ||
                !ota_parse_start_payload(payload, frame.length, &ota_header)) {
                ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_BAD_LENGTH);
                continue;
            }

            if ((s_ota_ctx.state == OTA_STATE_RECV_PAYLOAD) &&
                (ota_header.magic == OTA_FRAME_MAGIC) &&
                (ota_header.app_version == s_ota_ctx.app_version) &&
                (ota_header.app_size == s_ota_ctx.expected_size) &&
                (ota_header.app_crc32 == s_ota_ctx.expected_crc32)) {
                ota_send_ack(frame.seq, s_ota_ctx.recv_size);
                continue;
            }

            if (!ota_begin(&ota_header)) {
                ota_send_nack(frame.seq, 0U, OTA_NACK_FLASH);
                my_printf("OTA: START rejected\r\n");
                ota_uart_reset_state();
                return;
            }

            ota_send_ack(frame.seq, s_ota_ctx.recv_size);
        } else if (frame.type == OTA_FRAME_TYPE_DATA) {
            if (s_ota_ctx.state != OTA_STATE_RECV_PAYLOAD) {
                ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_BAD_STATE);
                continue;
            }

            if (((uint16_t)(s_ota_ctx.expected_seq - 1U) == frame.seq) &&
                ((frame.offset + frame.length) <= s_ota_ctx.recv_size)) {
                ota_send_ack(frame.seq, s_ota_ctx.recv_size);
                continue;
            }

            if (frame.seq != s_ota_ctx.expected_seq) {
                ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_BAD_SEQ);
                continue;
            }

            if (frame.offset != s_ota_ctx.recv_size) {
                ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_BAD_OFFSET);
                continue;
            }

            if (!ota_write_payload(payload, frame.length)) {
                ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_FLASH);
                my_printf("OTA: DATA write failed seq=%u offset=%lu\r\n", frame.seq, frame.offset);
                ota_uart_reset_state();
                return;
            }

            s_ota_ctx.expected_seq++;
            ota_send_ack(frame.seq, s_ota_ctx.recv_size);

            if (((s_ota_ctx.recv_size % 4096U) == 0U) || (s_ota_ctx.recv_size == s_ota_ctx.expected_size)) {
                my_printf("OTA: progress %lu/%lu (%lu%%)\r\n",
                          s_ota_ctx.recv_size,
                          s_ota_ctx.expected_size,
                          (s_ota_ctx.expected_size == 0U) ? 0U :
                          (s_ota_ctx.recv_size * 100U / s_ota_ctx.expected_size));
            }
        } else if (frame.type == OTA_FRAME_TYPE_END) {
            if ((s_ota_ctx.state != OTA_STATE_RECV_PAYLOAD) ||
                (frame.seq != s_ota_ctx.expected_seq) ||
                (frame.offset != s_ota_ctx.recv_size) ||
                (frame.length != 0U) ||
                (frame.crc32 != s_ota_ctx.expected_crc32)) {
                ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_FINAL);
                continue;
            }

            if (!ota_finalize_if_done()) {
                ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_FINAL);
                my_printf("OTA: END finalize failed\r\n");
                ota_uart_reset_state();
                return;
            }

            ota_send_ack(frame.seq, s_ota_ctx.recv_size);
            my_printf("OTA: done, reset to bootloader\r\n");
            __disable_irq();
            NVIC_SystemReset();
        } else {
            ota_send_nack(frame.seq, s_ota_ctx.recv_size, OTA_NACK_BAD_STATE);
        }

        ota_check_flow_control();
    }
}
