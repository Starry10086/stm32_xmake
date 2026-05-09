#ifndef OTA_UART_H
#define OTA_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ota_uart_reset_state(void);
void ota_uart_process_frame(const uint8_t *data, uint32_t len);
void ota_uart_task(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_UART_H */

