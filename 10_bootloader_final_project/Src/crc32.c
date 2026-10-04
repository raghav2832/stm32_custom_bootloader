#include "crc32.h"
#include "app_header.h"
#include <stdbool.h>

#define CRC32_POLY   0xEDB88320UL

static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void crc32_build_table(void)
{
    for (uint32_t n = 0; n < 256; n++)
    {
        uint32_t c = n;

        for (uint32_t k = 0; k < 8; k++)
        {
            c = (c & 1) ? (CRC32_POLY ^ (c >> 1)) : (c >> 1);
        }

        crc32_table[n] = c;
    }

    crc32_table_ready = true;
}

uint32_t crc32_compute_app_image(uint32_t base_addr, uint32_t length)
{
    const uint8_t *data = (const uint8_t *)base_addr;
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t magic_bytes[4];

    if (!crc32_table_ready)
    {
        crc32_build_table();
    }

    magic_bytes[0] = (uint8_t)(APP_HEADER_MAGIC & 0xFF);
    magic_bytes[1] = (uint8_t)((APP_HEADER_MAGIC >> 8) & 0xFF);
    magic_bytes[2] = (uint8_t)((APP_HEADER_MAGIC >> 16) & 0xFF);
    magic_bytes[3] = (uint8_t)((APP_HEADER_MAGIC >> 24) & 0xFF);

    for (uint32_t i = 0; i < length; i++)
    {
        uint8_t byte = data[i];

        if (i < 4U)
        {
            byte = magic_bytes[i];
        }
        else if ((i >= APP_HEADER_CRC_OFFSET) && (i < (APP_HEADER_CRC_OFFSET + 4U)))
        {
            byte = 0;
        }

        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFUL;
}
