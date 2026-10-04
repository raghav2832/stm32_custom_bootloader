#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>

uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint32_t length);
uint16_t crc16_compute(const uint8_t *data, uint32_t length);

#endif /* CRC16_H */
