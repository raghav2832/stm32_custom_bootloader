#ifndef APP_HEADER_H
#define APP_HEADER_H

#include <stdint.h>

#define APP_HEADER_MAGIC    0xB00710ADUL
#define APP_HEADER_SIZE     0x200UL
#define APP_HEADER_CRC_OFFSET   8U

typedef struct
{
    uint32_t magic;
    uint32_t image_size;
    uint32_t crc32;
    uint32_t version;
    uint32_t reserved[124];
} app_header_t;

#endif /* APP_HEADER_H */
