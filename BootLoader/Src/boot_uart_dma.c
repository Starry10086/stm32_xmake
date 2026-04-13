#include "boot_uart_dma.h"
#include "boot_config.h"
#include "boot_flash.h"
#include <string.h>

#define OTA_CMD_START 0x01
#define OTA_CMD_END 0x03
#define OTA_CMD_TIMEOUT 5000U

OTA_StateTypeDef OTA_State = OTA_STATE_IDLE;
uint32_t CurrentWriteAddr = APP_START_ADDR;

static uint8_t s_dma_buf[UART_DMA_BUF_SIZE];
static uint8_t s_ring_buf[UART_RINGBUFFER_SIZE];
static struct rt_ringbuffer s_ring;
static uint16_t s_old_pos = 0;

static void boot_uart_push_data(const uint8_t *data, uint16_t len){
    rt_ringbuffer_put(&s_ring, data, len);
}

void boot_uart_init(){
    rt_ringbuffer_init(&s_ring, s_ring_buf, UART_RINGBUFFER_SIZE);
    s_old_pos = 0;

    if(HAL_UART_Receive_DMA(&huart1, s_dma_buf, UART_DMA_BUF_SIZE) != HAL_OK){
        Error_Handler();
    }

    // 禁用DMA 半满/全满中断（因为循环模式不需要中断通知）
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_TC);
}
// 扫描DMA缓冲区，将数据推入环形缓冲区
void boot_uart_poll(){
    uint16_t cur_pos = UART_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx);

    if(cur_pos == s_old_pos)
        return;

    if(cur_pos > s_old_pos){
        boot_uart_push_data(&s_dma_buf[s_old_pos], cur_pos - s_old_pos);
    }
    else{
        boot_uart_push_data(&s_dma_buf[s_old_pos], UART_DMA_BUF_SIZE - s_old_pos);
        boot_uart_push_data(&s_dma_buf[0], cur_pos);
    }

    s_old_pos = cur_pos;
}
// 查询有多少数据可读
uint16_t boot_uart_available(){
    boot_uart_poll();
    return (uint16_t)rt_ringbuffer_data_len(&s_ring);
}
// 从缓冲区读取数据,返回读取到的数据长度
uint16_t boot_read_uart(uint8_t *buf, uint16_t len){
    boot_uart_poll();

    uint16_t avail = (uint16_t)rt_ringbuffer_data_len(&s_ring);
    if(avail == 0) return 0U;

    uint16_t to_read = (len < avail) ? len : avail;
    rt_ringbuffer_get(&s_ring, buf, to_read);
    return to_read;
}
// 发送数据
void boot_uart_write(const uint8_t *data, uint16_t len){
    // 阻塞式发送，直到发送完成
    HAL_UART_Transmit(&huart1, (uint8_t*)data, len, HAL_MAX_DELAY);
}
// 发送字符串
void boot_uart_print(const char *str){
    boot_uart_write((const uint8_t*)str, (uint16_t)strlen(str));
}
void boot_process_packet(){
    static uint8_t rx_buf[UART_RX_CHUNK_SIZE];

    switch(OTA_State){
        case OTA_STATE_IDLE:
            if(boot_uart_available() >= 1){
                uint8_t byte = 0;
                boot_read_uart(&byte, 1);
                if(byte == OTA_CMD_START){
                    OTA_State = OTA_STATE_ERASER;
                }
            }
        break;
        case OTA_STATE_ERASER:
            if(Flash_Erase() == 0){
                OTA_State = OTA_STATE_RECEVING;
                CurrentWriteAddr = APP_START_ADDR;
                boot_uart_print("Erase success\r\n");
            }
            else{
                OTA_State = OTA_STATE_IDLE;
                boot_uart_print("Erase failed\r\n");
            }
        break;
        case OTA_STATE_RECEVING:
            {
                static uint8_t s_leftover_buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
                static uint8_t s_leftover_cnt = 0;
                static uint32_t s_last_time = 0;

                if(s_last_time == 0){
                        s_last_time = HAL_GetTick();
                }
                if(HAL_GetTick() - s_last_time > OTA_CMD_TIMEOUT){
                        while(s_leftover_cnt < 4){
                            s_leftover_buf[s_leftover_cnt++] = 0xFF;
                        }
                        Flash_WriteData(CurrentWriteAddr, s_leftover_buf, 4);
                        OTA_State = OTA_STATE_DONE;
                        boot_uart_print("TIME OUT\r\n");
                        break;
                }

                if(boot_uart_available() == 0)
                    break;
                uint16_t n = boot_read_uart(rx_buf, UART_RX_CHUNK_SIZE);
                if(n == 0)
                    break;
                s_last_time = HAL_GetTick();
                if(n == 1 && rx_buf[0] == OTA_CMD_END){
                        while(s_leftover_cnt < 4){
                            s_leftover_buf[s_leftover_cnt++] = 0xFF;
                        }
                        Flash_WriteData(CurrentWriteAddr, s_leftover_buf, 4);
                        OTA_State = OTA_STATE_DONE;
                        boot_uart_print("END\r\n");
                        break;
                }

                uint16_t idx = 0;
                while(0 < s_leftover_cnt < 4 && idx < n){
                        s_leftover_buf[s_leftover_cnt++] = rx_buf[idx++];
                }
                if(s_leftover_cnt == 4){
                        Flash_WriteData(CurrentWriteAddr, s_leftover_buf, 4);
                        CurrentWriteAddr += 4;
                        s_leftover_cnt = 0;
                        memset(s_leftover_buf, 0xFF, 4);
                }
                uint16_t remain = n - idx;
                uint16_t bulk = remain & 0xFFFC;
                if(bulk > 0){
                        Flash_WriteData(CurrentWriteAddr, &rx_buf[idx], bulk);
                        CurrentWriteAddr += bulk;
                        idx += bulk;
                        remain -= bulk;
                }
                remain = n - idx;
                while(remain > 0){
                        s_leftover_buf[s_leftover_cnt++] = rx_buf[idx++];
                        remain--;
                }
            }
        break;
        case OTA_STATE_DONE:
            boot_uart_print("OTA done\r\n");
            HAL_Delay(100);
            Jump_App();
        break;
        default:
        break;
    }
}