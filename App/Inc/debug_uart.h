#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int my_printf(const char *format, ...);
void debug_uart_write(const uint8_t *data, uint16_t len);
void debug_uart_print(const char *str);
void uart_task(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_UART_H */
