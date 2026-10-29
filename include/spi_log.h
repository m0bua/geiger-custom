#ifndef SPI_LOG_H
#define SPI_LOG_H

#include "log_query.h"

#include <stdbool.h>
#include <stdint.h>

#define SPI_LOG_YEAR_VALID 0x20u
#define SPI_LOG_YEAR_MASK 0x1Fu
#define SPI_LOG_YEAR_DESCRIPTOR(year, valid) \
    ((uint8_t)(((valid) ? SPI_LOG_YEAR_VALID : 0u) | \
        ((uint16_t)(year) & SPI_LOG_YEAR_MASK)))

typedef enum {
    SPI_LOG_READY = 0u,
    SPI_LOG_NEEDS_EVICTION = 1u,
    SPI_LOG_ERROR = 2u
} spi_log_prepare_result_t;

typedef struct {
    uint32_t address;
    uint32_t generation;
    uint16_t epoch;
    uint8_t segment_id;
    uint8_t year_descriptor;
    uint16_t record_count;
} spi_log_sector_info_t;

/* Initializes the SPI log partitions and recovers each newest append sector. */
bool spi_log_init(void);
bool spi_log_open_readonly(void);
bool spi_log_read_fault(void);
bool spi_log_latest_epoch(uint16_t *epoch);
bool spi_log_clear(void);
bool spi_log_expire_alerts(uint8_t year);
uint32_t spi_log_count(log_record_kind_t kind);
bool spi_log_find(log_record_kind_t kind, uint16_t before_epoch,
                   uint32_t before_time, uint16_t skip, bool inclusive,
                   uint32_t date_limit, uint32_t reference, log_stored_t *item);
uint32_t spi_log_free_records(log_record_kind_t kind);

/* Ensure an append sector exists for this segment. If the reserved sector is
 * needed to preserve the one-spare-sector rule, returns its oldest sector in
 * `oldest`; the caller must cascade its records before erasing it. */
spi_log_prepare_result_t spi_log_prepare(log_record_kind_t kind,
                                         uint16_t epoch,
                                         uint8_t year_descriptor,
                                         spi_log_sector_info_t *oldest);

bool spi_log_append(log_record_kind_t kind, uint16_t epoch,
                    const log_record_t *record);
bool spi_log_find_oldest(log_record_kind_t kind,
                         spi_log_sector_info_t *sector);
bool spi_log_read_sector_record(log_record_kind_t kind,
                                const spi_log_sector_info_t *sector,
                                uint16_t index, log_record_t *record);
bool spi_log_erase_oldest(log_record_kind_t kind,
                          const spi_log_sector_info_t *sector);

/* Exact-record adapter for the common display/application query engine.
 * `epoch` is the segment ID; `period_start` uses seconds since 2000-01-01. */
bool spi_log_query_read(void *context, log_record_kind_t kind,
                        uint16_t epoch, uint32_t period_start,
                        log_record_t *record, bool *live);

#endif
