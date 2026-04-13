#ifndef __BOOT_CONFIG_H
#define __BOOT_CONFIG_H

#include "stm32f4xx_hal.h"

#define BOOT_START_ADDR 0x08000000UL
#define BOOT_END_ADDR   0x0800FFFFUL
#define APP_START_ADDR  0x08010000UL
#define APP_END_ADDR   0x080FFFFFUL
#define APP_MAX_SIZE    (APP_END_ADDR - APP_START_ADDR)
#define APP_SECTOR_CNT 8 // 8个扇区

#define SRAM_START_ADDR 0x20000000UL
#define SRAM_END_ADDR   0x20020000UL

#define UART_DMA_BUF_SIZE 256U
#define UART_RINGBUFFER_SIZE 1024U
#define UART_RX_CHUNK_SIZE 128U

#define BOOT_WAIT_MS 5000U

typedef enum{
    OTA_STATE_IDLE = 0,
    OTA_STATE_ERASER,
    OTA_STATE_RECEVING,
    OTA_STATE_DONE,
} OTA_StateTypeDef;

extern OTA_StateTypeDef OTA_State;
extern uint32_t CurrentWriteAddr;

#endif /* __BOOT_CONFIG_H */