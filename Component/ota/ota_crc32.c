#include "ota_crc32.h"

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t j;

    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (j = 0U; j < 8U; j++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

uint32_t ota_crc32_finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t ota_crc32_calc(const uint8_t *data, uint32_t len)
{
    return ota_crc32_finalize(ota_crc32_update(0xFFFFFFFFUL, data, len));
}
