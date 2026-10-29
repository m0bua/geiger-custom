#include "spi_nor.h"

#include "board.h"

#include <stddef.h>
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define RCC_BASE       0x40021000u
#define RCC_IOPENR     REG32(RCC_BASE + 0x2Cu)
#define RCC_APB2ENR    REG32(RCC_BASE + 0x34u)
#define GPIOA_BASE     0x50000000u
#define GPIOB_BASE     0x50000400u
#define GPIO_MODER     0x00u
#define GPIO_OTYPER    0x04u
#define GPIO_OSPEEDR   0x08u
#define GPIO_PUPDR     0x0Cu
#define GPIO_BSRR      0x18u
#define GPIO_AFRL      0x20u
#define SPI1_BASE      0x40013000u
#define SPI_CR1        0x00u
#define SPI_CR2        0x04u
#define SPI_SR         0x08u
#define SPI_DR         0x0Cu

#define NOR_CS_PIN 15u
#define NOR_PAGE_SIZE 256u
#define NOR_SECTOR_SIZE 4096u
#define NOR_CAPACITY_BYTES 0x00100000u
#define NOR_ID_MANUFACTURER 0xC2u
#define NOR_ID_MEMORY_TYPE  0x28u
#define NOR_ID_CAPACITY     0x14u

static bool nor_ready;
static spi_nor_jedec_id_t nor_id;

static void nor_cs(bool selected)
{
    REG32(GPIOA_BASE + GPIO_BSRR) = selected ?
        (1u << (16u + NOR_CS_PIN)) : (1u << NOR_CS_PIN);
}

static bool spi_transfer(uint8_t tx, uint8_t *rx)
{
    uint32_t timeout = 100000u;
    while ((REG32(SPI1_BASE + SPI_SR) & (1u << 1u)) == 0u &&
           --timeout != 0u) { }
    if (timeout == 0u) return false;

    *(volatile uint8_t *)(uintptr_t)(SPI1_BASE + SPI_DR) = tx;
    timeout = 100000u;
    while ((REG32(SPI1_BASE + SPI_SR) & 1u) == 0u && --timeout != 0u) { }
    if (timeout == 0u) return false;
    const uint8_t value = *(volatile uint8_t *)(uintptr_t)(SPI1_BASE + SPI_DR);
    timeout = 100000u;
    while ((REG32(SPI1_BASE + SPI_SR) & (1u << 7u)) != 0u &&
           --timeout != 0u) { }
    if (timeout == 0u) return false;
    if (rx != NULL) *rx = value;
    return true;
}

static bool nor_command(uint8_t command)
{
    bool ok;
    nor_cs(true);
    ok = spi_transfer(command, NULL);
    nor_cs(false);
    return ok;
}

static bool nor_status(uint8_t *status)
{
    bool ok;
    nor_cs(true);
    ok = spi_transfer(0x05u, NULL) && spi_transfer(0u, status);
    nor_cs(false);
    return ok;
}

static bool nor_wait_ready(uint32_t timeout_ms)
{
    const uint32_t deadline = board_millis + timeout_ms;
    do {
        uint8_t status;
        if (!nor_status(&status)) return false;
        if ((status & 1u) == 0u) return true;
    } while ((int32_t)(board_millis - deadline) < 0);
    return false;
}

static bool nor_write_enable(void)
{
    uint8_t status;
    return nor_command(0x06u) && nor_status(&status) &&
        (status & (1u << 1u)) != 0u;
}

bool spi_nor_init(void)
{
    nor_ready = false;
    RCC_IOPENR |= 3u;
    RCC_APB2ENR |= 1u << 12u;
    (void)RCC_APB2ENR;

    /* SPI1 NOR: PB3=SCK, PB4=MISO, PB5=MOSI, AF0; PA15 is active-low CS. */
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + NOR_CS_PIN);
    REG32(GPIOA_BASE + GPIO_OTYPER) &= ~(1u << NOR_CS_PIN);
    REG32(GPIOA_BASE + GPIO_OSPEEDR) |= 3u << (NOR_CS_PIN * 2u);
    REG32(GPIOA_BASE + GPIO_PUPDR) &= ~(3u << (NOR_CS_PIN * 2u));
    REG32(GPIOA_BASE + GPIO_MODER) =
        (REG32(GPIOA_BASE + GPIO_MODER) & ~(3u << (NOR_CS_PIN * 2u))) |
        (1u << (NOR_CS_PIN * 2u));

    REG32(GPIOB_BASE + GPIO_OTYPER) &= ~((1u << 3u) | (1u << 4u) |
                                        (1u << 5u));
    REG32(GPIOB_BASE + GPIO_OSPEEDR) |= (3u << 6u) | (3u << 8u) |
                                       (3u << 10u);
    REG32(GPIOB_BASE + GPIO_PUPDR) &= ~((3u << 6u) | (3u << 8u) |
                                       (3u << 10u));
    REG32(GPIOB_BASE + GPIO_MODER) =
        (REG32(GPIOB_BASE + GPIO_MODER) & ~((3u << 6u) | (3u << 8u) |
                                           (3u << 10u))) |
        (2u << 6u) | (2u << 8u) | (2u << 10u);
    REG32(GPIOB_BASE + GPIO_AFRL) &= ~((0xFu << 12u) | (0xFu << 16u) |
                                      (0xFu << 20u));

    REG32(SPI1_BASE + SPI_CR1) = 0u;
    REG32(SPI1_BASE + SPI_CR2) = 0u;
    /* Master, /8, software NSS high, mode 0, 8-bit, MSB first. */
    REG32(SPI1_BASE + SPI_CR1) = (1u << 2u) | (2u << 3u) |
                                 (1u << 8u) | (1u << 9u) | (1u << 6u);

    /* A previous stock session may have left the NOR in deep power-down. */
    if (!nor_command(0xABu)) {
        REG32(SPI1_BASE + SPI_CR1) &= ~(1u << 6u);
        return false;
    }
    {
        const uint32_t ready_at = board_millis + 3u;
        while ((int32_t)(board_millis - ready_at) < 0) { }
    }

    /* A controller reset can leave the flash finishing an earlier erase or
     * program operation. Wait before reading JEDEC or scanning log headers. */
    if (!nor_wait_ready(500u)) {
        REG32(SPI1_BASE + SPI_CR1) &= ~(1u << 6u);
        return false;
    }

    nor_cs(true);
    const bool id_read = spi_transfer(0x9Fu, NULL) &&
        spi_transfer(0u, &nor_id.manufacturer) &&
        spi_transfer(0u, &nor_id.memory_type) &&
        spi_transfer(0u, &nor_id.capacity);
    nor_cs(false);
    if (!id_read) {
        REG32(SPI1_BASE + SPI_CR1) &= ~(1u << 6u);
        return false;
    }

    /* The populated device reports the Macronix MX25R8035F JEDEC ID. */
    nor_ready = nor_id.manufacturer == NOR_ID_MANUFACTURER &&
        nor_id.memory_type == NOR_ID_MEMORY_TYPE &&
        nor_id.capacity == NOR_ID_CAPACITY;
    return nor_ready;
}

void spi_nor_deinit(void)
{
    nor_cs(false);
    REG32(SPI1_BASE + SPI_CR1) &= ~(1u << 6u);
    nor_ready = false;
}

bool spi_nor_is_ready(void)
{
    return nor_ready;
}

spi_nor_jedec_id_t spi_nor_jedec_id(void)
{
    return nor_id;
}

uint32_t spi_nor_size_bytes(void)
{
    /* Report the discovered JEDEC density independently of write readiness.
     * Debug UI can then distinguish a detected chip from a usable one. */
    if (nor_id.capacity < 0x10u || nor_id.capacity > 0x20u) return 0u;
    return 1u << nor_id.capacity;
}

bool spi_nor_read(uint32_t address, uint8_t *data, size_t length)
{
    if (!nor_ready || data == NULL || address > NOR_CAPACITY_BYTES ||
        length > NOR_CAPACITY_BYTES - address) return false;
    nor_cs(true);
    bool ok = spi_transfer(0x03u, NULL) &&
        spi_transfer((uint8_t)(address >> 16u), NULL) &&
        spi_transfer((uint8_t)(address >> 8u), NULL) &&
        spi_transfer((uint8_t)address, NULL);
    for (size_t i = 0u; ok && i < length; ++i) {
        ok = spi_transfer(0u, &data[i]);
    }
    nor_cs(false);
    return ok;
}

bool spi_nor_program_page(uint32_t address, const uint8_t *data,
                          size_t length)
{
    if (!nor_ready || data == NULL || length == 0u ||
        length > NOR_PAGE_SIZE || address > NOR_CAPACITY_BYTES ||
        length > NOR_CAPACITY_BYTES - address ||
        address / NOR_PAGE_SIZE != (address + length - 1u) / NOR_PAGE_SIZE ||
        !nor_wait_ready(10u) || !nor_write_enable()) return false;

    nor_cs(true);
    bool ok = spi_transfer(0x02u, NULL) &&
        spi_transfer((uint8_t)(address >> 16u), NULL) &&
        spi_transfer((uint8_t)(address >> 8u), NULL) &&
        spi_transfer((uint8_t)address, NULL);
    for (size_t i = 0u; ok && i < length; ++i) {
        ok = spi_transfer(data[i], NULL);
    }
    nor_cs(false);
    if (!ok || !nor_wait_ready(20u)) return false;

    /* Some NOR devices accept commands while a sector is protected but do
     * not change the array. Read back each programmed page so the logger can
     * fall back to EEPROM and report ERR instead of silently losing records. */
    {
        uint8_t verify[16];
        size_t offset = 0u;
        while (offset < length) {
            const size_t amount = length - offset < sizeof(verify) ?
                length - offset : sizeof(verify);
            if (!spi_nor_read(address + (uint32_t)offset, verify, amount)) {
                return false;
            }
            for (size_t i = 0u; i < amount; ++i) {
                if (verify[i] != data[offset + i]) return false;
            }
            offset += amount;
        }
    }
    return true;
}

bool spi_nor_erase_sector(uint32_t address)
{
    if (!nor_ready || address >= NOR_CAPACITY_BYTES ||
        (address % NOR_SECTOR_SIZE) != 0u || !nor_wait_ready(20u) ||
        !nor_write_enable()) return false;
    nor_cs(true);
    const bool ok = spi_transfer(0x20u, NULL) &&
        spi_transfer((uint8_t)(address >> 16u), NULL) &&
        spi_transfer((uint8_t)(address >> 8u), NULL) &&
        spi_transfer((uint8_t)address, NULL);
    nor_cs(false);
    if (!ok || !nor_wait_ready(500u)) return false;
    {
        uint8_t verify[16];
        if (!spi_nor_read(address, verify, sizeof(verify))) return false;
        for (size_t i = 0u; i < sizeof(verify); ++i) {
            if (verify[i] != 0xFFu) return false;
        }
    }
    return true;
}
