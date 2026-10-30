#include "dose_reconcile.h"

#include "log_history.h"
#include "log_record.h"
#include "log_writer.h"

typedef enum { RECONCILE_DAYS, RECONCILE_HOURS } reconcile_phase_t;

static log_cursor_t cursor;
static uint32_t day_start_time;
static uint32_t day_snapshot;
static uint32_t day_sum;
static uint32_t history_sum;
static uint32_t cached_history_sum;
static uint16_t active_day_key;
static uint16_t cached_day_key;
static uint8_t active_memory;
static uint8_t cached_memory;
static reconcile_phase_t phase;
static bool active;
static bool history_cached;

static uint32_t add_saturated(uint32_t left, uint32_t right)
{
    return UINT32_MAX - left < right ? UINT32_MAX : left + right;
}

void dose_reconcile_cancel(void)
{
    active = false;
}

void dose_reconcile_invalidate_history(void)
{
    history_cached = false;
}

void dose_reconcile_begin(uint8_t memory, uint16_t day_key,
                          uint32_t day_start, uint32_t day_pulse_snapshot)
{
    active_memory = memory;
    active_day_key = day_key;
    day_start_time = day_start;
    day_snapshot = day_pulse_snapshot;
    day_sum = 0u;
    if (history_cached && cached_day_key == day_key &&
        cached_memory == memory) {
        history_sum = cached_history_sum;
        phase = RECONCILE_HOURS;
    } else {
        history_sum = 0u;
        phase = RECONCILE_DAYS;
    }
    log_history_begin(&cursor);
    active = true;
}

/* Keep the scan out of main so LTO can fit the image in 64 KiB of Flash. */
dose_reconcile_result_t __attribute__((noinline)) dose_reconcile_step(uint8_t memory,
                          uint16_t day_key, uint32_t live_day_pulses,
                          uint32_t *day_pulses, uint32_t *total_pulses)
{
    log_point_t point;
    const uint32_t day_end = day_start_time + 86400u;
    if (!active) return DOSE_RECONCILE_IDLE;
    if (memory != active_memory || day_key != active_day_key ||
        log_writer_fault()) {
        active = false;
        return DOSE_RECONCILE_FAILED;
    }
    const bool reading_days = phase == RECONCILE_DAYS;
    if (log_history_next_from(active_memory,
            reading_days ? LOG_RECORD_DAY : LOG_RECORD_HOUR,
            reading_days ? 86400u : 3600u,
            &cursor, 0u, false, &point)) {
        if (reading_days) {
            if (!point.calendar_valid || point.period_start < day_start_time) {
                history_sum = add_saturated(history_sum, point.count);
            }
        } else if (point.calendar_valid && point.period_start >= day_start_time &&
                   point.period_start < day_end) {
            day_sum = add_saturated(day_sum, point.count);
        }
        return DOSE_RECONCILE_PENDING;
    }
    if (log_writer_fault()) {
        active = false;
        return DOSE_RECONCILE_FAILED;
    }
    if (reading_days) {
        cached_history_sum = history_sum;
        cached_day_key = active_day_key;
        cached_memory = active_memory;
        history_cached = true;
        phase = RECONCILE_HOURS;
        log_history_begin(&cursor);
        return DOSE_RECONCILE_PENDING;
    }
    if (day_pulses == 0 || total_pulses == 0) {
        active = false;
        return DOSE_RECONCILE_FAILED;
    }
    const uint32_t recent = live_day_pulses >= day_snapshot ?
        live_day_pulses - day_snapshot : 0u;
    *day_pulses = add_saturated(day_sum, recent);
    *total_pulses = add_saturated(history_sum, *day_pulses);
    active = false;
    return DOSE_RECONCILE_COMPLETE;
}
