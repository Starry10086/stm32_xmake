#include "debug_uart.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_debug_rx_flag = 0U;
static uint8_t s_debug_rx_buffer[512] = {0};

void debug_uart_write(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }

    (void)HAL_UART_Transmit(&huart2, (uint8_t *)data, len, HAL_MAX_DELAY);
}

void debug_uart_print(const char *str)
{
    if (str == NULL) {
        return;
    }

    debug_uart_write((const uint8_t *)str, (uint16_t)strlen(str));
}

int my_printf(const char *format, ...)
{
    char buffer[512];
    va_list arg;
    int len;
    uint16_t tx_len;

    if (format == NULL) {
        return 0;
    }

    va_start(arg, format);
    len = vsnprintf(buffer, sizeof(buffer), format, arg);
    va_end(arg);

    if (len <= 0) {
        return len;
    }

    tx_len = (len >= (int)sizeof(buffer)) ? (uint16_t)(sizeof(buffer) - 1U) : (uint16_t)len;
    debug_uart_write((const uint8_t *)buffer, tx_len);
    return len;
}

void uart_task(void)
{
    if (s_debug_rx_flag == 0U) {
        return;
    }

    my_printf("%s", s_debug_rx_buffer);
    memset(s_debug_rx_buffer, 0, sizeof(s_debug_rx_buffer));
    s_debug_rx_flag = 0U;
}
