#include "dose_reconcile.h"

#include "log_history.h"
#include "log_record.h"
#include "log_writer.h"

static log_cursor_t cursor;
static uint32_t day_start_time;
static uint32_t day_sum;
static uint32_t total_sum;
static uint16_t active_day_key;
static uint8_t active_memory;
static bool active;
static bool first_point;
static bool ready;

static uint32_t add_saturated(uint32_t left, uint32_t right)
{
    return UINT32_MAX - left < right ? UINT32_MAX : left + right;
}

void dose_reconcile_cancel(void)
{
    active = false;
}

void dose_reconcile_begin(uint8_t memory, uint16_t day_key,
                          uint32_t day_start)
{
    active_memory = memory;
    active_day_key = day_key;
    day_start_time = day_start;
    day_sum = 0u;
    total_sum = 0u;
    first_point = true;
    ready = false;
    log_history_begin(&cursor);
    active = true;
}

void dose_reconcile_use_summary(uint8_t memory, uint16_t day_key,
                                uint32_t day_count, uint32_t total_count)
{
    active_memory = memory;
    active_day_key = day_key;
    day_sum = day_count;
    total_sum = total_count;
    active = ready = true;
}

/* Keep the scan out of main so LTO can fit the image in 64 KiB of Flash. */
dose_reconcile_result_t __attribute__((noinline)) dose_reconcile_step(uint8_t memory,
                          uint16_t day_key,
                          uint32_t *day_pulses, uint32_t *total_pulses)
{
    log_point_t point;
    if (!active) return DOSE_RECONCILE_IDLE;
    if (memory != active_memory || day_key != active_day_key ||
        log_writer_fault()) {
        active = false;
        return DOSE_RECONCILE_FAILED;
    }
    /* Read exactly the day points used by the graph, including its live day. */
    if (!ready && log_history_next_from(active_memory, LOG_RECORD_DAY, 86400u,
            &cursor, 0u, true, &point)) {
        if (first_point) {
            if (point.calendar_valid && point.period_start == day_start_time)
                day_sum = point.count;
            first_point = false;
        }
        total_sum = add_saturated(total_sum, point.count);
        return DOSE_RECONCILE_PENDING;
    }
    if (log_writer_fault()) {
        active = false;
        return DOSE_RECONCILE_FAILED;
    }
    if (day_pulses == 0 || total_pulses == 0) {
        active = false;
        return DOSE_RECONCILE_FAILED;
    }
    *day_pulses = day_sum;
    *total_pulses = total_sum;
    active = false;
    return DOSE_RECONCILE_COMPLETE;
}
