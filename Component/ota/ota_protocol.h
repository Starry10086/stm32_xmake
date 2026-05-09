#ifndef OTA_UART_PROTOCOL_H
#define OTA_UART_PROTOCOL_H

#include <stdint.h>

#if defined(__CC_ARM)
#define OTA_PACKED_STRUCT_BEGIN     __packed struct
#define OTA_PACKED_STRUCT_END
#else
#define OTA_PACKED_STRUCT_BEGIN     struct
#define OTA_PACKED_STRUCT_END       __attribute__((packed))
#endif

#define OTA_FRAME_MAGIC             0x5AA5C33CUL
#define OTA_FRAME_MAX_PAYLOAD       512U

/*
 * UART OTA protocol:
 *   START carries image metadata.
 *   DATA carries firmware bytes in increasing seq/offset order.
 *   END confirms total size and image CRC before reset to BootLoader.
 * ACK/NACK are sent with the current receive offset so the sender can retry.
 */
#define OTA_FRAME_TYPE_START        0x01U
#define OTA_FRAME_TYPE_DATA         0x02U
#define OTA_FRAME_TYPE_END          0x03U
#define OTA_FRAME_TYPE_ACK          0x81U
#define OTA_FRAME_TYPE_NACK         0x82U

#define OTA_NACK_BAD_STATE          1U
#define OTA_NACK_BAD_CRC            2U
#define OTA_NACK_BAD_SEQ            3U
#define OTA_NACK_BAD_OFFSET         4U
#define OTA_NACK_BAD_LENGTH         5U
#define OTA_NACK_FLASH              6U
#define OTA_NACK_FINAL              7U

#define OTA_IMAGE_HEADER_V1_SIZE    16U
#define OTA_IMAGE_HEADER_V2_VERSION 2U
#define OTA_IMAGE_TYPE_APP1         1U
#define OTA_HW_ID_ANY               0U

/* V1 start payload kept for compatibility with the simple sender format. */
typedef OTA_PACKED_STRUCT_BEGIN {
    uint32_t magic;
    uint32_t app_version;
    uint32_t app_size;
    uint32_t app_crc32;
} OTA_PACKED_STRUCT_END ota_header_t;

/* Fixed frame header. DATA crc32 covers payload only, not this header. */
typedef OTA_PACKED_STRUCT_BEGIN {
    uint32_t magic;
    uint8_t type;
    uint8_t status;
    uint16_t seq;
    uint32_t offset;
    uint16_t length;
    uint16_t reserved;
    uint32_t crc32;
} OTA_PACKED_STRUCT_END ota_frame_header_t;

/* V2 start payload adds target checks before writing the image to APP2. */
typedef OTA_PACKED_STRUCT_BEGIN {
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t app_version;
    uint32_t app_size;
    uint32_t app_crc32;
    uint32_t target_addr;
    uint32_t image_type;
    uint32_t hw_id;
    uint32_t header_crc32;
} OTA_PACKED_STRUCT_END ota_image_header_v2_t;

#endif /* OTA_UART_PROTOCOL_H */
