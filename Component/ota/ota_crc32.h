#ifndef OTA_CRC32_H
#define OTA_CRC32_H

#include <stdint.h>

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t ota_crc32_finalize(uint32_t crc);
uint32_t ota_crc32_calc(const uint8_t *data, uint32_t len);

#endif
