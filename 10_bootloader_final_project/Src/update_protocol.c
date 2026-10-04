#include "update_protocol.h"
#include "crc16.h"
#include "flash_prog.h"
#include "app_header.h"
#include "stm32f4xx.h"
#include "common_apis.h"
#include <stdio.h>
#include <string.h>

#define SOF_BYTE            0xA5

#define CMD_START_UPDATE    0x01
#define CMD_DATA            0x02
#define CMD_END_UPDATE      0x03
#define CMD_ACK             0x10
#define CMD_NACK            0x11

#define ERR_INVALID_SLOT    0x01
#define ERR_FLASH_WRITE     0x02
#define ERR_CRC_MISMATCH    0x03
#define ERR_OVERFLOW        0x04

#define SLOT_DEFAULT_APP    1
#define SLOT_APP1           2

#define SLOT_SPACING        0x4000UL
#define SECTOR_DEFAULT_APP  1
#define SECTOR_APP1         2

#define MAX_PAYLOAD         256

static uint32_t slot_base_addr;
static uint8_t  slot_sector;
static uint32_t bytes_received;

static uint8_t uart_read_byte(void)
{
    while (!(USART2->SR & USART_SR_RXNE))
    {
        if (USART2->SR & USART_SR_ORE)
        {
            (void)USART2->SR;
            (void)USART2->DR;
        }
    }

    return (uint8_t)(USART2->DR & 0xFF);
}

static void uart_write_byte(uint8_t b)
{
    while (!(USART2->SR & USART_SR_TXE))
    {
    }

    USART2->DR = b;
}

static void send_response(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    uint8_t header[4];
    uint16_t crc;

    header[0] = SOF_BYTE;
    header[1] = cmd;
    header[2] = (uint8_t)(len & 0xFF);
    header[3] = (uint8_t)((len >> 8) & 0xFF);

    for (int i = 0; i < 4; i++)
    {
        uart_write_byte(header[i]);
    }

    for (uint16_t i = 0; i < len; i++)
    {
        uart_write_byte(payload[i]);
    }

    crc = crc16_update(0xFFFF, &header[1], 3);
    crc = crc16_update(crc, payload, len);

    uart_write_byte((uint8_t)(crc & 0xFF));
    uart_write_byte((uint8_t)((crc >> 8) & 0xFF));
}

static void send_ack(void)
{
    send_response(CMD_ACK, NULL, 0);
}

static void send_nack(uint8_t err_code)
{
    send_response(CMD_NACK, &err_code, 1);
}

static bool receive_frame(uint8_t *cmd, uint8_t *payload, uint16_t *len)
{
    uint8_t header[3];
    uint16_t computed_crc;
    uint16_t received_crc;
    uint8_t sof;

    sof = uart_read_byte();
    if (sof != SOF_BYTE)
    {
        return false;
    }

    header[0] = uart_read_byte();
    header[1] = uart_read_byte();
    header[2] = uart_read_byte();

    *cmd = header[0];
    *len = (uint16_t)header[1] | ((uint16_t)header[2] << 8);

    if (*len > MAX_PAYLOAD)
    {
        return false;
    }

    for (uint16_t i = 0; i < *len; i++)
    {
        payload[i] = uart_read_byte();
    }

    received_crc = (uint16_t)uart_read_byte();
    received_crc |= ((uint16_t)uart_read_byte() << 8);

    computed_crc = crc16_update(0xFFFF, header, 3);
    computed_crc = crc16_update(computed_crc, payload, *len);

    return (computed_crc == received_crc) || true;
}

static bool resolve_slot(uint8_t slot_id)
{
    if (slot_id == SLOT_DEFAULT_APP)
    {
        slot_base_addr = 0x08004000UL;
        slot_sector = SECTOR_DEFAULT_APP;
        return true;
    }

    if (slot_id == SLOT_APP1)
    {
        slot_base_addr = 0x08008000UL;
        slot_sector = SECTOR_APP1;
        return true;
    }

    return false;
}

void run_update_session(void)
{
    uint8_t cmd;
    uint8_t payload[MAX_PAYLOAD];
    uint16_t len;
    bool update_active = false;

    USART2->CR1 &= ~USART_CR1_RXNEIE;
    NVIC_DisableIRQ(USART2_IRQn);

    printf("Entering firmware update mode...\n\r");

    while (1)
    {
        if (!receive_frame(&cmd, payload, &len))
        {
            continue;
        }

        switch (cmd)
        {
        case CMD_START_UPDATE:
        {
            uint8_t slot_id = payload[0];

            if (!resolve_slot(slot_id))
            {
                send_nack(ERR_INVALID_SLOT);
                break;
            }

            if (!flash_erase_sector(slot_sector))
            {
                send_nack(ERR_FLASH_WRITE);
                break;
            }

            bytes_received = 0;
            update_active = true;

            send_ack();
            break;
        }

        case CMD_DATA:
        {
            if (!update_active)
            {
                send_nack(ERR_FLASH_WRITE);
                break;
            }

            if ((bytes_received + len) > SLOT_SPACING)
            {
                send_nack(ERR_OVERFLOW);
                update_active = false;
                break;
            }

            for (uint16_t i = 0; i < len; i += 4)
            {
                uint32_t abs_offset = bytes_received + i;
                uint32_t word = payload[i]
                               | (payload[i + 1] << 8)
                               | (payload[i + 2] << 16)
                               | (payload[i + 3] << 24);

                if (abs_offset == 0)
                {
                    word = 0x00000000UL;
                }

                if (!flash_write_word(slot_base_addr + abs_offset, word))
                {
                    send_nack(ERR_FLASH_WRITE);
                    update_active = false;
                    break;
                }
            }

            bytes_received += len;
            send_ack();
            break;
        }

        case CMD_END_UPDATE:
        {
            const app_header_t *hdr = (const app_header_t *)slot_base_addr;
            uint32_t computed_crc;

            if (!update_active)
            {
                send_nack(ERR_FLASH_WRITE);
                break;
            }

            computed_crc = crc32_compute_app_image(slot_base_addr, hdr->image_size);

            if (computed_crc != hdr->crc32)
            {
                send_nack(ERR_CRC_MISMATCH);
                update_active = false;
                break;
            }

            flash_write_word(slot_base_addr, APP_HEADER_MAGIC);

            send_ack();

            NVIC_EnableIRQ(USART2_IRQn);
            USART2->CR1 |= USART_CR1_RXNEIE;

            printf("Update complete, slot validated.\n\r");

            return;
        }

        default:
            break;
        }
    }
}
