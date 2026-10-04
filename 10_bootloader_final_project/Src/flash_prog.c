#include "flash_prog.h"
#include "stm32f4xx.h"

#define FLASH_KEY1   0x45670123UL
#define FLASH_KEY2   0xCDEF89ABUL

static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_wait_busy(void)
{
    while (FLASH->SR & FLASH_SR_BSY)
    {
    }
}

bool flash_erase_sector(uint8_t sector)
{
    flash_wait_busy();
    flash_unlock();

    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PSIZE_1;

    FLASH->CR &= ~FLASH_CR_SNB;
    FLASH->CR |= (sector << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;

    flash_wait_busy();

    FLASH->CR &= ~FLASH_CR_SER;
    flash_lock();

    return (FLASH->SR & 0xF3) == 0;
}

bool flash_write_word(uint32_t addr, uint32_t data)
{
    flash_wait_busy();
    flash_unlock();

    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PSIZE_1;
    FLASH->CR |= FLASH_CR_PG;

    *(volatile uint32_t *)addr = data;

    flash_wait_busy();

    FLASH->CR &= ~FLASH_CR_PG;
    flash_lock();

    return (FLASH->SR & 0xF3) == 0;
}
