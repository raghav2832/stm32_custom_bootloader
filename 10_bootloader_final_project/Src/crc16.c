#include "crc16.h"

uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i];

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

uint16_t crc16_compute(const uint8_t *data, uint32_t length)
{
    return crc16_update(0xFFFF, data, length);
}
