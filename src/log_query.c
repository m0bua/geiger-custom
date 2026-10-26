#include "log_query.h"

#include <limits.h>

typedef struct {
    uint64_t count;
    uint32_t maximum_cpm;
    uint32_t covered_seconds;
    bool complete;
    bool live;
} log_accumulator_t;

static uint32_t clamp_count(uint64_t count)
{
    return count > UINT32_MAX ? UINT32_MAX : (uint32_t)count;
}

static bool add_record(log_query_read_fn read_record, void *context,
                       log_record_kind_t kind, uint16_t epoch,
                       uint32_t period_start, uint32_t period_seconds,
                       log_accumulator_t *accumulator)
{
    log_record_t record;
    bool live = false;
    uint32_t coverage;

    if (!read_record(context, kind, epoch, period_start, &record, &live)) {
        accumulator->complete = false;
        return false;
    }

    if (live && record.duration_seconds == 0u) {
        accumulator->complete = false;
        accumulator->live = true;
        return true;
    }

    coverage = live && record.duration_seconds != 0u ?
        record.duration_seconds : period_seconds;
    if (coverage > period_seconds) coverage = period_seconds;
    accumulator->count += record.count;
    if (record.maximum_cpm > accumulator->maximum_cpm) {
        accumulator->maximum_cpm = record.maximum_cpm;
    }
    accumulator->covered_seconds += coverage;
    accumulator->live = accumulator->live || live;
    if (coverage != period_seconds) accumulator->complete = false;
    return true;
}

static bool aggregate_hour(log_query_read_fn read_record, void *context,
                           uint16_t epoch, uint32_t start,
                           log_accumulator_t *result)
{
    log_accumulator_t minutes = {0u, 0u, 0u, true, false};
    bool all_minutes = true;

    for (uint8_t minute = 0u; minute < 60u; ++minute) {
        const uint32_t minute_start = start + (uint32_t)minute * 60u;
        if (!add_record(read_record, context, LOG_RECORD_MINUTE, epoch,
                minute_start, 60u, &minutes)) {
            all_minutes = false;
        }
    }
    if (all_minutes && minutes.complete) {
        *result = minutes;
        return true;
    }

    {
        log_accumulator_t hour = {0u, 0u, 0u, true, false};
        if (!minutes.live && add_record(read_record, context, LOG_RECORD_HOUR, epoch, start,
                3600u, &hour)) {
            *result = hour;
            return true;
        }
    }

    *result = minutes;
    result->complete = false;
    return minutes.covered_seconds != 0u;
}

static bool aggregate_day(log_query_read_fn read_record, void *context,
                          uint16_t epoch, uint32_t start,
                          log_accumulator_t *result)
{
    log_accumulator_t hours = {0u, 0u, 0u, true, false};
    bool all_hours = true;

    for (uint8_t hour_index = 0u; hour_index < 24u; ++hour_index) {
        log_accumulator_t hour = {0u, 0u, 0u, true, false};
        const uint32_t hour_start = start + (uint32_t)hour_index * 3600u;
        if (!aggregate_hour(read_record, context, epoch, hour_start, &hour)) {
            all_hours = false;
            hours.complete = false;
            continue;
        }
        hours.count += hour.count;
        if (hour.maximum_cpm > hours.maximum_cpm) {
            hours.maximum_cpm = hour.maximum_cpm;
        }
        hours.covered_seconds += hour.covered_seconds;
        hours.live = hours.live || hour.live;
        if (!hour.complete) {
            all_hours = false;
            hours.complete = false;
        }
    }
    if (all_hours && hours.complete) {
        *result = hours;
        return true;
    }

    {
        log_accumulator_t day = {0u, 0u, 0u, true, false};
        if (!hours.live && add_record(read_record, context, LOG_RECORD_DAY, epoch, start,
                86400u, &day)) {
            *result = day;
            return true;
        }
    }

    *result = hours;
    result->complete = false;
    return hours.covered_seconds != 0u;
}

static bool aggregate_range(log_query_read_fn read_record, void *context,
                            uint16_t epoch, uint32_t start, uint32_t seconds,
                            log_accumulator_t *result)
{
    uint32_t cursor = start;
    uint32_t remaining = seconds;
    bool any = false;

    *result = (log_accumulator_t){0u, 0u, 0u, true, false};
    while (remaining != 0u) {
        log_accumulator_t part = {0u, 0u, 0u, true, false};
        uint32_t step = 60u;
        bool found;

        if (remaining >= 86400u && cursor % 86400u == 0u) {
            step = 86400u;
            found = aggregate_day(read_record, context, epoch, cursor, &part);
        } else if (remaining >= 3600u && cursor % 3600u == 0u) {
            step = 3600u;
            found = aggregate_hour(read_record, context, epoch, cursor, &part);
        } else {
            found = add_record(read_record, context, LOG_RECORD_MINUTE,
                epoch, cursor, step, &part);
        }

        if (found) any = true;
        result->count += part.count;
        if (part.maximum_cpm > result->maximum_cpm) {
            result->maximum_cpm = part.maximum_cpm;
        }
        result->covered_seconds += part.covered_seconds;
        result->live = result->live || part.live;
        if (!found || !part.complete) result->complete = false;
        cursor += step;
        remaining -= step;
    }
    if (result->covered_seconds != seconds) result->complete = false;
    return any;
}

bool log_query_aggregate(log_query_read_fn read_record, void *context,
                         uint16_t epoch, uint32_t period_start,
                         uint32_t period_seconds, log_point_t *point)
{
    log_accumulator_t aggregate;
    bool found;
    if (read_record == NULL || point == NULL || period_seconds < 60u ||
        period_seconds % 60u != 0u || period_seconds > LOG_QUERY_MAX_PERIOD_SECONDS) return false;
    found = aggregate_range(read_record, context, epoch, period_start,
        period_seconds, &aggregate);
    *point = (log_point_t){
        .period_start = period_start,
        .period_seconds = period_seconds,
        .count = clamp_count(aggregate.count),
        .average_cpm = log_record_average_cpm(
            clamp_count(aggregate.count), aggregate.covered_seconds),
        .maximum_cpm = aggregate.maximum_cpm,
        .covered_seconds = aggregate.covered_seconds,
        .epoch = epoch,
        .available = found || aggregate.covered_seconds != 0u || aggregate.live,
        .partial = !found || !aggregate.complete,
        .live = aggregate.live
    };
    return true;
}

bool log_query_read(log_query_read_fn read_record, void *context,
                    uint16_t epoch, uint32_t newest_time, uint8_t n,
                    uint32_t m, log_point_t *points)
{
    uint32_t newest_start;

    if (read_record == NULL || points == NULL || n == 0u || m < 60u ||
        m % 60u != 0u || m > LOG_QUERY_MAX_PERIOD_SECONDS) return false;
    newest_start = newest_time - (newest_time % m);

    for (uint8_t index = 0u; index < n; ++index) {
        const uint64_t back = (uint64_t)index * m;
        log_accumulator_t aggregate;
        if (back > newest_start) {
            points[index] = (log_point_t){
                .period_start = 0u,
                .period_seconds = m,
                .epoch = epoch,
                .partial = true
            };
            continue;
        }
        const uint32_t start = newest_start - (uint32_t)back;
        const bool found = aggregate_range(read_record, context, epoch,
            start, m, &aggregate);
        points[index].period_start = start;
        points[index].period_seconds = m;
        points[index].count = clamp_count(aggregate.count);
        points[index].average_cpm = log_record_average_cpm(
            points[index].count, aggregate.covered_seconds);
        points[index].maximum_cpm = aggregate.maximum_cpm;
        points[index].covered_seconds = aggregate.covered_seconds;
        points[index].epoch = epoch;
        points[index].available = found || aggregate.covered_seconds != 0u || aggregate.live;
        points[index].partial = !found || !aggregate.complete;
        points[index].live = aggregate.live;
    }
    return true;
}
