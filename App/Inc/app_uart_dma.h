#ifndef APP_UART_DMA_H
#define APP_UART_DMA_H

/*
@brief 这个模块实现了一个基于DMA的UART接收机制，专门用来接收OTA升级数据帧。
       它会持续监测DMA缓冲区的数据变化，并把新收到的数据交给OTA协议处理函数进行解析和处理。
       同时也提供了一个简单的发送函数，可以用来发送数据。
*/
#include <stdint.h>

void app_uart_dma_init(void);
void app_uart_dma_poll(void);
void app_uart_dma_write(const uint8_t *data, uint16_t len);

#endif
