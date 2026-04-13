#ifndef __BOOT_UART_DMA_H
#define __BOOT_UART_DMA_H

#include "usart.h"
#include "main.h"
#include "ringbuffer.h"

void boot_uart_init();
void boot_uart_poll();
uint16_t boot_uart_available();
uint16_t boot_read_uart(uint8_t *buf, uint16_t len);
void boot_uart_write(const uint8_t *data, uint16_t len);
void boot_uart_print(const char *str);
void boot_process_packet();

#endif /* __BOOT_UART_DMA_H */