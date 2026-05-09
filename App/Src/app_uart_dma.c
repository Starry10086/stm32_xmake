#include "app_uart_dma.h"
#include "usart.h"
#include "ota_uart.h"
#include "main.h"

#define APP_OTA_DMA_BUF_SIZE 2048U

static uint8_t s_dma_buf[APP_OTA_DMA_BUF_SIZE];
static uint16_t s_old_pos = 0U;

void app_uart_dma_init(void)
{
    s_old_pos = 0U;

    if (HAL_UART_Receive_DMA(&huart1, s_dma_buf, APP_OTA_DMA_BUF_SIZE) != HAL_OK) {
        Error_Handler();
    }

    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_TC);
}

void app_uart_dma_poll(void)
{
    uint16_t cur_pos;

    cur_pos = (uint16_t)(APP_OTA_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx));
    if (cur_pos == s_old_pos) {
        return;
    }

    if (cur_pos > s_old_pos) {
        ota_uart_process_frame(&s_dma_buf[s_old_pos], (uint32_t)(cur_pos - s_old_pos));
    } else {
        ota_uart_process_frame(&s_dma_buf[s_old_pos], (uint32_t)(APP_OTA_DMA_BUF_SIZE - s_old_pos));
        if (cur_pos > 0U) {
            ota_uart_process_frame(&s_dma_buf[0], cur_pos);
        }
    }

    s_old_pos = cur_pos;
}

void app_uart_dma_write(const uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)data, len, HAL_MAX_DELAY);
}
