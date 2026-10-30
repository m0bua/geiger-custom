#include "log_writer.h"

#include "board.h"
#include "log_eeprom.h"
#include "log_eeprom_layout.h"
#include "log_record.h"
#include "spi_log.h"
#include "spi_nor.h"
#include "log_history.h"

#include <limits.h>

typedef struct {
    uint32_t minute_key;
    uint32_t period_index;
    uint32_t count;
    uint32_t maximum_cpm;
    uint64_t seconds_mask;
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
static bool writer_fault;
static uint8_t pruned_year = UINT8_MAX;
static uint16_t current_epoch;
static bool have_clock_sample;
static uint32_t previous_uptime;
static uint32_t previous_calendar;
static bool previous_calendar_valid;
static uint8_t active_log_memory = 0xFFu;
static bool hour_closed_event;

static bool minute_location(uint32_t seconds, bool calendar_valid,
                            uint32_t *minute_key, uint32_t *period_index,
                            uint8_t *year_descriptor)
{
    *minute_key = seconds / 60u;
    return log_record_location(LOG_RECORD_MINUTE, seconds, calendar_valid, period_index, year_descriptor);
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
                          const log_record_t *record);

#define SPI_LAYOUT_TAG_0 0xD4u
#define SPI_LAYOUT_TAG_1 0x2Bu

static bool format_spi_log_layout(void)
{
    return spi_log_clear() && log_eeprom_reset(false) &&
        board_eeprom_write(LOG_EEPROM_SPI_LAYOUT_TAG_BASE, SPI_LAYOUT_TAG_0) &&
        board_eeprom_write(LOG_EEPROM_SPI_LAYOUT_TAG_BASE + 1u, SPI_LAYOUT_TAG_1);
}

static bool prepare_spi_log_layout(void)
{
    uint8_t tag0, tag1;
    if (!board_eeprom_read(LOG_EEPROM_SPI_LAYOUT_TAG_BASE, &tag0) ||
        !board_eeprom_read(LOG_EEPROM_SPI_LAYOUT_TAG_BASE + 1u, &tag1)) return false;
    if (tag0 == SPI_LAYOUT_TAG_0 && tag1 == SPI_LAYOUT_TAG_1) return spi_log_init();
    return format_spi_log_layout();
}

static bool should_persist_period(log_record_kind_t kind)
{
    return kind == LOG_RECORD_HOUR || kind == LOG_RECORD_DAY;
}

static bool append_record(log_record_kind_t kind, uint16_t epoch,
                          uint8_t year_descriptor,
                          const log_record_t *record)
{
    if (active_log_memory == BOARD_LOG_MEMORY_EEPROM) {
        (void)epoch;
        return log_eeprom_append(kind, epoch, record, year_descriptor);
    }
    if (active_log_memory != BOARD_LOG_MEMORY_SPI) return false;
    spi_log_sector_info_t oldest;
    spi_log_prepare_result_t prepared = spi_log_prepare(kind, epoch,
        year_descriptor, &oldest);

    if (prepared == SPI_LOG_NEEDS_EVICTION) {
        if (!spi_log_erase_oldest(kind, &oldest)) return false;
        prepared = spi_log_prepare(kind, epoch, year_descriptor, &oldest);
    }
    return prepared == SPI_LOG_READY &&
        spi_log_append(kind, epoch, record);
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
    bool live = false;
    if (!accumulator->active || !should_persist_period(kind)) return;
    time_context.calendar_valid =
        (accumulator->year_descriptor & SPI_LOG_YEAR_VALID) != 0u;
    read_record = active_log_memory == BOARD_LOG_MEMORY_EEPROM ?
        log_eeprom_query_read : spi_log_query_read;
    if (read_record(&time_context, kind, accumulator->epoch,
            accumulator->period_start, &existing, &live)) return;
    if (!log_query_aggregate(read_record, &time_context,
            accumulator->epoch, accumulator->period_start, period_seconds,
            &aggregate) || !aggregate.available || aggregate.live ||
        aggregate.covered_seconds == 0u) return;
    record.period_index = accumulator->period_index;
    record.count = aggregate.count;
    record.maximum_cpm = aggregate.maximum_cpm;
    record.duration_seconds = 0u;
    record.segment_id = accumulator->segment_id;
    record.covered_seconds = aggregate.covered_seconds;
    if (!append_record(kind, accumulator->epoch,
        accumulator->year_descriptor, &record)) {
        writer_fault = true;
    } else if (kind == LOG_RECORD_HOUR) {
        hour_closed_event = true;
    }
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
    if (!log_record_location(kind, source_time, calendar_valid, &period_index, &year_descriptor)) return;
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
    if (!minute_accumulator.active || minute_accumulator.samples == 0u) {
        return;
    }
    record.period_index = minute_accumulator.period_index;
    record.count = minute_accumulator.count;
    record.maximum_cpm = minute_accumulator.maximum_cpm;
    record.duration_seconds = 0u;
    record.segment_id = minute_accumulator.segment_id;
    record.covered_seconds = minute_accumulator.samples;
    if (!append_record(LOG_RECORD_MINUTE, minute_accumulator.epoch,
        minute_accumulator.year_descriptor, &record)) writer_fault = true;
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
    record.covered_seconds = 0u;
    if (!append_record(LOG_RECORD_ALERT, alert_accumulator.epoch,
        alert_accumulator.year_descriptor, &record)) writer_fault = true;
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
    minute_accumulator.seconds_mask = 0u;
    minute_accumulator.epoch = current_epoch;
    minute_accumulator.segment_id =
        (uint8_t)(current_epoch % LOG_SEGMENT_ID_COUNT);
    minute_accumulator.year_descriptor = year_descriptor;
    minute_accumulator.calendar_valid = calendar_valid;
    log_record_t existing;
    bool live = false;
    log_query_time_context_t context = {calendar_valid};
    log_query_read_fn reader = active_log_memory == BOARD_LOG_MEMORY_SPI ? spi_log_query_read : log_eeprom_query_read;
    if (reader(&context, LOG_RECORD_MINUTE, current_epoch, key * 60u, &existing, &live)) {
        minute_accumulator.count = existing.count;
        minute_accumulator.maximum_cpm = existing.maximum_cpm;
        minute_accumulator.samples = (uint16_t)(existing.covered_seconds > 60u ?
            60u : existing.covered_seconds);
        minute_accumulator.seconds_mask = minute_accumulator.samples == 60u ?
            (UINT64_C(1) << 60u) - 1u : 0u;
    }
    minute_accumulator.active = true;
}

bool log_writer_init(void)
{
    uint16_t latest_epoch = 0u;
    bool have_epoch = false;
    const uint8_t log_memory = board_display_log_memory();
    writer_ready = false;
    writer_fault = false;
    pruned_year = UINT8_MAX;
    minute_accumulator.active = false;
    alert_accumulator.active = false;
    hour_accumulator.active = false;
    day_accumulator.active = false;
    have_clock_sample = false;
    hour_closed_event = false;
    active_log_memory = log_memory;

    if (!log_eeprom_init()) goto failed;
    if (log_memory == BOARD_LOG_MEMORY_OFF) {
        if (board_display_log_source() == BOARD_LOG_MEMORY_SPI &&
            (!(spi_nor_is_ready() || spi_nor_init()) ||
             !prepare_spi_log_layout())) goto failed;
        return true;
    }
    if (log_memory == BOARD_LOG_MEMORY_EEPROM) {
        current_epoch = log_eeprom_active_epoch(&latest_epoch) ?
            latest_epoch : 0u;
        writer_ready = true;
        return true;
    }
    if (log_memory != BOARD_LOG_MEMORY_SPI) goto failed;
    if (!prepare_spi_log_layout()) goto failed;
    have_epoch = spi_log_latest_epoch(&latest_epoch);
    current_epoch = have_epoch ? latest_epoch : 0u;
    writer_ready = true;
    return true;
failed:
    writer_fault = true;
    return false;
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
    if (source_time != 0u) --source_time;

    if (have_clock_sample && calendar_valid && previous_calendar_valid) {
        const uint32_t uptime_delta = uptime_seconds - previous_uptime;
        const uint32_t expected = previous_calendar + uptime_delta;
        const int64_t correction = (int64_t)calendar_seconds - expected;
        if (correction > 30 || correction < -30) {
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
    if (calendar_valid && pruned_year != year_descriptor) {
        bool ok = active_log_memory == BOARD_LOG_MEMORY_SPI ?
            spi_log_expire_alerts(year_descriptor) : log_eeprom_expire_alerts(year_descriptor);
        if (!ok) writer_fault = true;
        pruned_year = year_descriptor;
    }
    if (!minute_accumulator.active) {
        start_minute(source_time, calendar_valid);
    } else if (minute_accumulator.minute_key != minute_key ||
            minute_accumulator.epoch != current_epoch ||
            minute_accumulator.year_descriptor != year_descriptor) {
        close_minute();
        if (year_descriptor != minute_accumulator.year_descriptor) {
            close_period(LOG_RECORD_HOUR, &hour_accumulator);
            close_period(LOG_RECORD_DAY, &day_accumulator);
            ++current_epoch;
            hour_accumulator.active = day_accumulator.active = false;
        }
        start_minute(source_time, calendar_valid);
    }

    update_period_tracker(LOG_RECORD_HOUR, &hour_accumulator, source_time, calendar_valid);
    update_period_tracker(LOG_RECORD_DAY, &day_accumulator, source_time, calendar_valid);
    if (minute_accumulator.active) {
        if (UINT32_MAX - minute_accumulator.count < pulses) {
            minute_accumulator.count = UINT32_MAX;
        } else {
            minute_accumulator.count += pulses;
        }
        if (cpm > minute_accumulator.maximum_cpm) {
            minute_accumulator.maximum_cpm = cpm;
        }
        uint64_t bit = UINT64_C(1) << (source_time % 60u);
        if ((minute_accumulator.seconds_mask & bit) == 0u) {
            minute_accumulator.seconds_mask |= bit;
            if (minute_accumulator.samples < 60u) ++minute_accumulator.samples;
        }
    }

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

    if (source_time % 60u == 59u) {
        close_minute();
        uint32_t next = source_time + 1u;
        uint32_t next_key, next_index;
        uint8_t next_year;
        update_period_tracker(LOG_RECORD_HOUR, &hour_accumulator, next, calendar_valid);
        update_period_tracker(LOG_RECORD_DAY, &day_accumulator, next, calendar_valid);
        if (minute_location(next, calendar_valid, &next_key, &next_index, &next_year) &&
            next_year != minute_accumulator.year_descriptor) {
            ++current_epoch;
            hour_accumulator.active = day_accumulator.active = false;
        }
        start_minute(next, calendar_valid);
    }

    previous_uptime = uptime_seconds;
    previous_calendar = calendar_seconds;
    previous_calendar_valid = calendar_valid;
    have_clock_sample = true;
}

bool log_writer_live(log_record_kind_t kind, log_stored_t *item)
{
    if (item == NULL || !writer_ready) return false;
    if (kind == LOG_RECORD_MINUTE && minute_accumulator.active) {
        *item = (log_stored_t){
            .record = {minute_accumulator.period_index, minute_accumulator.count,
                minute_accumulator.maximum_cpm, minute_accumulator.samples,
                minute_accumulator.segment_id, minute_accumulator.samples},
            .time = minute_accumulator.minute_key * 60u,
            .epoch = minute_accumulator.epoch,
            .year = minute_accumulator.year_descriptor, .id = UINT16_MAX
        };
        return true;
    }
    if (kind == LOG_RECORD_ALERT && alert_accumulator.active && alert_accumulator.duration) {
        uint32_t reference = previous_calendar_valid ? previous_calendar : previous_uptime;
        uint32_t time;
        if (!log_record_time(kind, alert_accumulator.minute_index, alert_accumulator.year_descriptor, reference, &time)) return false;
        *item = (log_stored_t){
            .record = {alert_accumulator.minute_index, alert_accumulator.count,
                alert_accumulator.maximum_cpm, alert_accumulator.duration, alert_accumulator.segment_id},
            .time = time, .epoch = alert_accumulator.epoch, .year = alert_accumulator.year_descriptor, .id = UINT16_MAX
        };
        return true;
    }
    return false;
}

bool log_writer_change_memory(uint8_t target)
{
    uint8_t source = board_display_log_source();
    if (target > BOARD_LOG_MEMORY_OFF) return false;
    if (target == BOARD_LOG_MEMORY_OFF) return true;
    if (!log_eeprom_init()) return false;
    if (target == source) {
        if (target == BOARD_LOG_MEMORY_SPI)
            return (spi_nor_is_ready() || spi_nor_init()) &&
                prepare_spi_log_layout();
        return true;
    }
    if (target == BOARD_LOG_MEMORY_SPI) {
        return (spi_nor_is_ready() || spi_nor_init()) &&
            format_spi_log_layout();
    }
    return log_eeprom_reset(false);
}

void log_writer_memory_committed(uint8_t target)
{
    active_log_memory = target;
    writer_ready = target != BOARD_LOG_MEMORY_OFF;
    minute_accumulator.active = false;
    alert_accumulator.active = false;
    hour_accumulator.active = false;
    have_clock_sample = false;
    day_accumulator.active = false;
    hour_closed_event = false;
}

bool log_writer_take_hour_closed(void)
{
    const bool closed = hour_closed_event;
    hour_closed_event = false;
    return closed;
}

bool log_writer_fault(void)
{
    return writer_fault || (board_display_log_source() == BOARD_LOG_MEMORY_SPI &&
        spi_log_read_fault());
}
