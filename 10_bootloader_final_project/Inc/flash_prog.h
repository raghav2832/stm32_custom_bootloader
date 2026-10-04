#ifndef FLASH_PROG_H
#define FLASH_PROG_H

#include <stdint.h>
#include <stdbool.h>

bool flash_erase_sector(uint8_t sector);
bool flash_write_word(uint32_t addr, uint32_t data);

#endif /* FLASH_PROG_H */
