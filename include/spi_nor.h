#ifndef SPI_NOR_H
#define SPI_NOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t manufacturer;
    uint8_t memory_type;
    uint8_t capacity;
} spi_nor_jedec_id_t;

bool spi_nor_init(void);
void spi_nor_deinit(void);
bool spi_nor_is_ready(void);
spi_nor_jedec_id_t spi_nor_jedec_id(void);
uint32_t spi_nor_size_bytes(void);
bool spi_nor_read(uint32_t address, uint8_t *data, size_t length);
bool spi_nor_program_page(uint32_t address, const uint8_t *data,
                          size_t length);
bool spi_nor_erase_sector(uint32_t address);

#endif
