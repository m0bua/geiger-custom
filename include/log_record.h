#ifndef LOG_RECORD_H
#define LOG_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The segment id is a reference into the bounded year/epoch table. It keeps
 * year data out of each minute record while distinguishing repeated times. */
#define LOG_RECORD_LEGACY_DATA_BYTES 8u
#define LOG_RECORD_DATA_BYTES 9u
#define LOG_ALERT_DATA_BYTES 11u
#define LOG_RECORD_CPM_CEILING 262144u
#define LOG_SEGMENT_ID_COUNT 64u
#define LOG_YEAR_VALID 0x20u
#define LOG_YEAR_MASK 0x1Fu

typedef enum {
    LOG_RECORD_MINUTE = 0u,
    LOG_RECORD_HOUR = 1u,
    LOG_RECORD_DAY = 2u,
    LOG_RECORD_ALERT = 3u
} log_record_kind_t;

typedef struct {
    uint32_t period_index;
    uint32_t count;
    uint32_t maximum_cpm;
    uint16_t duration_seconds;
    uint8_t segment_id;
    uint32_t covered_seconds;
} log_record_t;

uint16_t log_crc16(const uint8_t *data, uint16_t length, uint16_t initial);
size_t log_record_data_size(log_record_kind_t kind);
uint32_t log_record_period_seconds(log_record_kind_t kind);
uint32_t log_record_count_ceiling(log_record_kind_t kind);
bool log_record_encode(log_record_kind_t kind, const log_record_t *record,
                       uint8_t *data, size_t capacity);
bool log_record_decode(log_record_kind_t kind, const uint8_t *data,
                       size_t length, log_record_t *record);
uint32_t log_record_average_cpm(uint32_t count, uint32_t duration_seconds);

typedef struct {
    log_record_t record;
    uint32_t time;
    uint16_t epoch;
    uint8_t year;
    uint16_t id;
} log_stored_t;

bool log_record_location(log_record_kind_t kind, uint32_t seconds,
                          bool calendar_valid, uint32_t *index, uint8_t *year);
bool log_record_time(log_record_kind_t kind, uint32_t index, uint8_t year,
                      uint32_t reference, uint32_t *seconds);
#endif
