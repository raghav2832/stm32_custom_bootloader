#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

uint32_t crc32_compute_app_image(uint32_t base_addr, uint32_t length);

#endif /* CRC32_H */
