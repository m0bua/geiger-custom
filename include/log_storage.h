#ifndef LOG_STORAGE_H
#define LOG_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#define LOG_CUTOFF_REASON_BOOT 1u
#define LOG_CUTOFF_REASON_CLOCK_SET 2u

typedef enum {
    LOG_CLEAR_MINUTE = 0,
    LOG_CLEAR_HOUR,
    LOG_CLEAR_DAY,
    LOG_CLEAR_ERROR,
    LOG_CLEAR_LOGS,
    LOG_CLEAR_EEPROM_LOGS,
    LOG_CLEAR_SPI
} log_clear_target_t;

typedef enum {
    LOG_GRAPH_MINUTES = 0,
    LOG_GRAPH_HOURS,
    LOG_GRAPH_DAYS
} log_graph_period_t;

void log_storage_init(bool use_spi);
void log_storage_set_spi_enabled(bool enabled);
void log_storage_sample_cpm(uint32_t cpm);
void log_storage_second(uint32_t pulse_count);
/* Persist RAM aggregates when their owning medium is available.  MIN/HOUR
 * live snapshots belong to SPI; DAY belongs to EEPROM. */
void log_storage_checkpoint(uint32_t calendar_seconds, bool calendar_valid,
                            bool usb_powered, bool screen_woke);
void log_storage_restore_live(uint32_t calendar_seconds, bool calendar_valid);
void log_storage_flush(void);
void log_storage_alert_sample(uint32_t cpm, uint32_t pulse_count,
                              uint32_t uptime_seconds, bool rate_valid);
bool log_storage_spi_available(void);
uint16_t log_storage_minute_records_in_sector(void);
/* Persisted history only: SPI MIN/HOUR and EEPROM DAY, excluding RAM/LIVE. */
void log_storage_log_counts(uint16_t *minutes, uint16_t *hours,
                            uint16_t *days);
/* Physical SPI alert records (snapshots), not distinct alarm episodes. */
uint16_t log_storage_alert_record_count(void);
/* Loads up to capacity completed minute CPM values, oldest first. */
bool log_storage_minute_history_load(uint32_t *cpm, uint8_t capacity,
                                     uint8_t *count);
/* Loads the current RAM period first, followed by persisted history newest
 * first. LIVE checkpoints are excluded. Rates are packed as two 18-bit CPM
 * values in five bytes per entry: minimum followed by maximum. */
bool log_storage_graph_load(log_graph_period_t period, uint32_t *dose,
                            uint8_t *packed_min_max_cpm, uint8_t capacity,
                            uint8_t *count);
uint32_t log_storage_graph_current_seconds(log_graph_period_t period);
bool log_storage_clear(log_clear_target_t target);
void log_storage_time_cutoff(uint32_t uptime_seconds, uint32_t calendar_seconds,
                             bool calendar_valid, uint8_t reason);
bool log_storage_calendar_day(uint16_t day_key, bool at_runtime_boundary);
void log_storage_today_seed(uint32_t pulse_count);
uint32_t log_storage_today_pulse_count(void);
uint32_t log_storage_today_recover_from_hours(uint8_t completed_hours);

#endif
