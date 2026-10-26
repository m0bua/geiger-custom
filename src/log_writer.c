#include "log_writer.h"

#include "board.h"
#include "log_eeprom.h"
#include "log_record.h"
#include "spi_log.h"

#include <limits.h>

typedef struct {
    uint32_t minute_key;
    uint32_t period_index;
    uint32_t count;
    uint32_t maximum_cpm;
    uint16_t samples;
    uint16_t epoch;
    uint8_t year_descriptor;
    uint8_t segment_id;
    bool calendar_valid;
    bool active;
} minute_accumulator_t;

typedef struct {
    uint32_t minute_index;
    uint32_t count;
    uint32_t maximum_cpm;
    uint16_t duration;
    uint16_t epoch;
    uint8_t year_descriptor;
    uint8_t segment_id;
    bool active;
} alert_accumulator_t;

typedef struct {
    uint32_t key;
    uint32_t period_start;
    uint32_t period_index;
    uint16_t epoch;
    uint8_t year_descriptor;
    uint8_t segment_id;
    bool active;
} period_accumulator_t;

static minute_accumulator_t minute_accumulator;
static alert_accumulator_t alert_accumulator;
static period_accumulator_t hour_accumulator;
static period_accumulator_t day_accumulator;
static bool writer_ready;
static uint16_t current_epoch;
static bool have_clock_sample;
static uint32_t previous_uptime;
static uint32_t previous_calendar;
static bool previous_calendar_valid;
static uint16_t current_calendar_year;
static uint8_t active_log_memory = 0xFFu;

static uint8_t leap_year(uint16_t year)
{
    return (uint8_t)(((year % 4u) == 0u && (year % 100u) != 0u) ||
        (year % 400u) == 0u);
}

static bool calendar_fields(uint32_t seconds, uint16_t *year_out,
                            uint32_t *day_of_year, uint32_t *day_seconds)
{
    uint32_t days = seconds / 86400u;
    uint16_t year = 2000u;
    while (year <= 2099u) {
        const uint16_t year_days = leap_year(year) != 0u ? 366u : 365u;
        if (days < year_days) break;
        days -= year_days;
        ++year;
    }
    if (year > 2099u) return false;
    *year_out = year;
    *day_of_year = days;
    *day_seconds = seconds % 86400u;
    return true;
}

static bool minute_location(uint32_t seconds, bool calendar_valid,
                            uint32_t *minute_key, uint32_t *period_index,
                            uint8_t *year_descriptor)
{
    if (calendar_valid) {
        uint16_t year;
        uint32_t day_of_year;
        uint32_t day_seconds;
        if (!calendar_fields(seconds, &year, &day_of_year, &day_seconds)) {
            return false;
        }
        current_calendar_year = year;
        *minute_key = seconds / 60u;
        *period_index = day_of_year * 1440u + day_seconds / 60u;
        *year_descriptor = SPI_LOG_YEAR_DESCRIPTOR(year, true);
    } else {
        *minute_key = seconds / 60u;
        *period_index = *minute_key % 527040u;
        *year_descriptor = 0u;
    }
    return true;
}

static uint8_t current_led_level(void)
{
    const uint8_t mask = board_display_rate_led_mask();
    /* Display masks: red=1, yellow=3, green=2. */
    if (mask == 1u) return 2u;
    if (mask == 3u) return 1u;
    if (mask == 2u) return 0u;
    return 0xFFu;
}

static bool append_record(log_record_kind_t kind, uint16_t epoch,
                          uint8_t year_descriptor,
                          const log_record_t *record)
{
    if (active_log_memory == BOARD_LOG_MEMORY_EEPROM) {
        (void)epoch;
        return log_eeprom_append(kind, record, year_descriptor);
    }
    if (active_log_memory != BOARD_LOG_MEMORY_SPI) return false;
    spi_log_sector_info_t oldest;
    spi_log_prepare_result_t prepared = spi_log_prepare(kind, epoch,
        year_descriptor, &oldest);

    if (prepared == SPI_LOG_NEEDS_EVICTION) {
        if (kind == LOG_RECORD_MINUTE) {
            for (uint16_t index = 0u; index < oldest.record_count; ++index) {
                log_record_t first;
                log_record_t hour;
                uint16_t year = current_calendar_year;
                uint32_t year_start = 0u;
                uint32_t hour_index;
                bool complete = true;
                bool existing_live = false;
                if (!spi_log_read_sector_record(kind, &oldest, index,
                        &first) ||
                    (oldest.year_descriptor & SPI_LOG_YEAR_VALID) == 0u) {
                    continue;
                }
                while (year > 2000u &&
                    (year & SPI_LOG_YEAR_MASK) !=
                        (oldest.year_descriptor & SPI_LOG_YEAR_MASK)) {
                    --year;
                }
                if ((year & SPI_LOG_YEAR_MASK) !=
                        (oldest.year_descriptor & SPI_LOG_YEAR_MASK)) continue;
                for (uint16_t y = 2000u; y < year; ++y) {
                    year_start += (uint32_t)(leap_year(y) != 0u ? 366u : 365u) *
                        86400u;
                }
                if (first.period_index % 60u != 0u) continue;
                hour_index = first.period_index / 60u;
                hour.period_index = hour_index;
                hour.count = 0u;
                hour.maximum_cpm = 0u;
                hour.duration_seconds = 0u;
                hour.segment_id = first.segment_id;
                for (uint8_t minute = 0u; minute < 60u; ++minute) {
                    log_record_t part;
                    bool live = false;
                    const uint32_t period_start = year_start +
                        hour_index * 3600u + (uint32_t)minute * 60u;
                    if (!spi_log_query_read(NULL, LOG_RECORD_MINUTE,
                            oldest.epoch, period_start, &part, &live) || live ||
                        part.segment_id != first.segment_id) {
                        complete = false;
                        break;
                    }
                    hour.count += part.count;
                    if (part.maximum_cpm > hour.maximum_cpm) {
                        hour.maximum_cpm = part.maximum_cpm;
                    }
                }
                if (!complete || spi_log_query_read(NULL, LOG_RECORD_HOUR,
                        oldest.epoch, year_start + hour_index * 3600u,
                        &hour, &existing_live)) continue;
                if (!append_record(LOG_RECORD_HOUR, oldest.epoch,
                        oldest.year_descriptor, &hour)) return false;
            }
        }
        if (!spi_log_erase_oldest(kind, &oldest)) return false;
        prepared = spi_log_prepare(kind, epoch, year_descriptor, &oldest);
    }
    return prepared == SPI_LOG_READY &&
        spi_log_append(kind, epoch, record);
}

static uint32_t free_records(log_record_kind_t kind)
{
    if (active_log_memory == BOARD_LOG_MEMORY_EEPROM) {
        return (uint32_t)log_eeprom_capacity(kind) -
            log_eeprom_count(kind);
    }
    return spi_log_free_records(kind);
}

static void close_period(log_record_kind_t kind,
                         period_accumulator_t *accumulator)
{
    log_record_t record;
    log_record_t existing;
    log_point_t aggregate;
    log_query_time_context_t time_context;
    log_query_read_fn read_record;
    const uint32_t period_seconds = log_record_period_seconds(kind);
    uint32_t lower_free;
    uint32_t lower_required;
    bool live = false;
    if (!accumulator->active) return;
    if (kind == LOG_RECORD_HOUR) {
        lower_free = free_records(LOG_RECORD_MINUTE);
        lower_required = 60u;
    } else {
        if (active_log_memory != BOARD_LOG_MEMORY_EEPROM) return;
        lower_free = free_records(LOG_RECORD_HOUR);
        lower_required = 24u;
    }
    if (lower_free >= lower_required) return;
    time_context.calendar_valid =
        (accumulator->year_descriptor & SPI_LOG_YEAR_VALID) != 0u;
    read_record = active_log_memory == BOARD_LOG_MEMORY_EEPROM ?
        log_eeprom_query_read : spi_log_query_read;
    if (read_record(&time_context, kind, accumulator->epoch,
            accumulator->period_start, &existing, &live)) return;
    if (!log_query_aggregate(read_record, &time_context,
            accumulator->epoch, accumulator->period_start, period_seconds,
            &aggregate) || !aggregate.available || aggregate.partial ||
        aggregate.live || aggregate.covered_seconds != period_seconds) return;
    record.period_index = accumulator->period_index;
    record.count = aggregate.count;
    record.maximum_cpm = aggregate.maximum_cpm;
    record.duration_seconds = 0u;
    record.segment_id = accumulator->segment_id;
    (void)append_record(kind, accumulator->epoch,
        accumulator->year_descriptor, &record);
}

static void update_period_tracker(log_record_kind_t kind,
                                  period_accumulator_t *tracker,
                                  uint32_t source_time, bool calendar_valid)
{
    const uint32_t seconds_per_period = kind == LOG_RECORD_HOUR ? 3600u :
        86400u;
    uint32_t key = source_time / seconds_per_period;
    const uint32_t period_start = source_time -
        (source_time % seconds_per_period);
    uint32_t period_index;
    uint8_t year_descriptor;
    if (calendar_valid) {
        uint16_t year;
        uint32_t day_of_year;
        uint32_t day_seconds;
        if (!calendar_fields(source_time, &year, &day_of_year, &day_seconds)) {
            return;
        }
        year_descriptor = SPI_LOG_YEAR_DESCRIPTOR(year, true);
        period_index = kind == LOG_RECORD_HOUR ?
            day_of_year * 24u + day_seconds / 3600u : day_of_year;
    } else {
        year_descriptor = 0u;
        period_index = key % (kind == LOG_RECORD_HOUR ? 8784u : 366u);
    }
    if (tracker->active && (tracker->key != key ||
            tracker->epoch != current_epoch ||
            tracker->year_descriptor != year_descriptor)) {
        close_period(kind, tracker);
        tracker->active = false;
    }
    if (!tracker->active) {
        tracker->key = key;
        tracker->period_start = period_start;
        tracker->period_index = period_index;
        tracker->epoch = current_epoch;
        tracker->segment_id =
            (uint8_t)(current_epoch % LOG_SEGMENT_ID_COUNT);
        tracker->year_descriptor = year_descriptor;
        tracker->active = true;
    }
}

static void close_minute(void)
{
    log_record_t record;
    if (!minute_accumulator.active || minute_accumulator.samples != 60u) {
        return;
    }
    record.period_index = minute_accumulator.period_index;
    record.count = minute_accumulator.count;
    record.maximum_cpm = minute_accumulator.maximum_cpm;
    record.duration_seconds = 0u;
    record.segment_id = minute_accumulator.segment_id;
    (void)append_record(LOG_RECORD_MINUTE, minute_accumulator.epoch,
        minute_accumulator.year_descriptor, &record);
}

static void close_alert(void)
{
    log_record_t record;
    if (!alert_accumulator.active || alert_accumulator.duration == 0u) return;
    record.period_index = alert_accumulator.minute_index;
    record.count = alert_accumulator.count;
    record.maximum_cpm = alert_accumulator.maximum_cpm;
    record.duration_seconds = alert_accumulator.duration;
    record.segment_id = alert_accumulator.segment_id;
    (void)append_record(LOG_RECORD_ALERT, alert_accumulator.epoch,
        alert_accumulator.year_descriptor, &record);
    alert_accumulator.active = false;
}

static void start_minute(uint32_t time_seconds, bool calendar_valid)
{
    uint32_t key;
    uint32_t period_index;
    uint8_t year_descriptor;
    if (!minute_location(time_seconds, calendar_valid, &key, &period_index,
            &year_descriptor)) return;
    minute_accumulator.minute_key = key;
    minute_accumulator.period_index = period_index;
    minute_accumulator.count = 0u;
    minute_accumulator.maximum_cpm = 0u;
    minute_accumulator.samples = 0u;
    minute_accumulator.epoch = current_epoch;
    minute_accumulator.segment_id =
        (uint8_t)(current_epoch % LOG_SEGMENT_ID_COUNT);
    minute_accumulator.year_descriptor = year_descriptor;
    minute_accumulator.calendar_valid = calendar_valid;
    minute_accumulator.active = true;
}

bool log_writer_init(void)
{
    uint16_t latest_epoch = 0u;
    uint8_t latest_segment = 0u;
    bool have_epoch = false;
    const uint8_t log_memory = board_display_log_memory();
    writer_ready = false;
    minute_accumulator.active = false;
    alert_accumulator.active = false;
    hour_accumulator.active = false;
    day_accumulator.active = false;
    have_clock_sample = false;
    current_calendar_year = 2000u;
    active_log_memory = log_memory;

    if (log_memory == BOARD_LOG_MEMORY_OFF) return false;
    if (log_memory == BOARD_LOG_MEMORY_EEPROM) {
        if (!log_eeprom_init()) return false;
        current_epoch = log_eeprom_active_segment(&latest_segment) ?
            latest_segment : 0u;
        writer_ready = true;
        return true;
    }
    if (log_memory != BOARD_LOG_MEMORY_SPI) return false;
    if (!spi_log_init()) return false;
    have_epoch = spi_log_latest_epoch(&latest_epoch);
    current_epoch = have_epoch ? latest_epoch : 0u;
    writer_ready = true;
    return true;
}

void log_writer_second(uint32_t pulses, uint32_t cpm,
                       uint32_t uptime_seconds, uint32_t calendar_seconds,
                       bool calendar_valid)
{
    uint32_t source_time;
    uint32_t minute_key;
    uint32_t period_index;
    uint8_t year_descriptor;
    uint8_t alarm_level;

    if (active_log_memory != board_display_log_memory()) {
        (void)log_writer_init();
    }
    if (!writer_ready) return;
    if (!have_clock_sample && !calendar_valid) ++current_epoch;
    source_time = calendar_valid ? calendar_seconds : uptime_seconds;

    if (have_clock_sample && calendar_valid && previous_calendar_valid) {
        const uint32_t uptime_delta = uptime_seconds - previous_uptime;
        const uint32_t expected = previous_calendar + uptime_delta;
        const int64_t correction = (int64_t)calendar_seconds - expected;
        if (correction > 60 || correction < -60) {
            close_alert();
            close_minute();
            close_period(LOG_RECORD_HOUR, &hour_accumulator);
            close_period(LOG_RECORD_DAY, &day_accumulator);
            ++current_epoch;
            minute_accumulator.active = false;
            hour_accumulator.active = false;
            day_accumulator.active = false;
        }
    } else if (have_clock_sample && calendar_valid != previous_calendar_valid) {
        close_alert();
        close_minute();
        close_period(LOG_RECORD_HOUR, &hour_accumulator);
        close_period(LOG_RECORD_DAY, &day_accumulator);
        ++current_epoch;
        minute_accumulator.active = false;
        hour_accumulator.active = false;
        day_accumulator.active = false;
    }

    if (!minute_location(source_time, calendar_valid, &minute_key,
            &period_index, &year_descriptor)) return;
    if (!minute_accumulator.active) {
        start_minute(source_time, calendar_valid);
    } else if (minute_accumulator.minute_key != minute_key ||
            minute_accumulator.epoch != current_epoch ||
            minute_accumulator.year_descriptor != year_descriptor) {
        close_minute();
        start_minute(source_time, calendar_valid);
    }

    if (minute_accumulator.active) {
        if (UINT32_MAX - minute_accumulator.count < pulses) {
            minute_accumulator.count = UINT32_MAX;
        } else {
            minute_accumulator.count += pulses;
        }
        if (cpm > minute_accumulator.maximum_cpm) {
            minute_accumulator.maximum_cpm = cpm;
        }
        if (minute_accumulator.samples < 60u) ++minute_accumulator.samples;
    }

    update_period_tracker(LOG_RECORD_HOUR, &hour_accumulator, source_time,
        calendar_valid);
    update_period_tracker(LOG_RECORD_DAY, &day_accumulator, source_time,
        calendar_valid);

    alarm_level = current_led_level();
    if (!alert_accumulator.active && alarm_level >= 1u && alarm_level <= 2u) {
        alert_accumulator.active = true;
        alert_accumulator.minute_index = period_index;
        alert_accumulator.count = 0u;
        alert_accumulator.maximum_cpm = cpm;
        alert_accumulator.duration = 0u;
        alert_accumulator.epoch = current_epoch;
        alert_accumulator.segment_id =
            (uint8_t)(current_epoch % LOG_SEGMENT_ID_COUNT);
        alert_accumulator.year_descriptor = year_descriptor;
    }
    if (alert_accumulator.active) {
        if (UINT32_MAX - alert_accumulator.count < pulses) {
            alert_accumulator.count = UINT32_MAX;
        } else {
            alert_accumulator.count += pulses;
        }
        if (cpm > alert_accumulator.maximum_cpm) {
            alert_accumulator.maximum_cpm = cpm;
        }
        if (alert_accumulator.duration < 4096u) {
            ++alert_accumulator.duration;
        }
        if (alarm_level == 0u || alert_accumulator.duration == 4096u) {
            close_alert();
            if (alarm_level != 0u) {
                alert_accumulator.active = true;
                alert_accumulator.minute_index = period_index;
                alert_accumulator.count = 0u;
                alert_accumulator.maximum_cpm = cpm;
                alert_accumulator.duration = 0u;
                alert_accumulator.epoch = current_epoch;
                alert_accumulator.segment_id =
                    (uint8_t)(current_epoch % LOG_SEGMENT_ID_COUNT);
                alert_accumulator.year_descriptor = year_descriptor;
            }
        }
    }

    previous_uptime = uptime_seconds;
    previous_calendar = calendar_seconds;
    previous_calendar_valid = calendar_valid;
    have_clock_sample = true;
}
