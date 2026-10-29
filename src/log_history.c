#include "log_history.h"

#include "board.h"
#include "log_eeprom.h"
#include "log_writer.h"
#include "spi_log.h"

typedef struct {
    log_query_time_context_t time;
    uint8_t memory;
    bool live;
    uint32_t date_limit;
} history_reader_t;

void log_history_begin(log_cursor_t *cursor)
{
    *cursor = (log_cursor_t){UINT32_MAX, UINT16_MAX, 0u};
}

static bool newer(const log_stored_t *a, const log_stored_t *b)
{
    return a->epoch > b->epoch || (a->epoch == b->epoch && a->time > b->time);
}

static bool read_record(void *context, log_record_kind_t kind, uint16_t epoch,
                         uint32_t start, log_record_t *record, bool *live)
{
    history_reader_t *reader = context;
    if (reader->date_limit && start > reader->date_limit) return false;
    log_stored_t current;
    if (reader->live && kind == LOG_RECORD_MINUTE && log_writer_live(kind, &current) &&
        current.epoch == epoch && current.time == start &&
        ((current.year & LOG_YEAR_VALID) != 0u) == reader->time.calendar_valid) {
        *record = current.record; *live = true; return true;
    }
    if (reader->memory == BOARD_LOG_MEMORY_SPI)
        return spi_log_query_read(&reader->time, kind, epoch, start, record, live);
    return log_eeprom_query_read(&reader->time, kind, epoch, start, record, live);
}

bool log_history_next_from(uint8_t memory, uint8_t type, uint32_t period,
                            log_cursor_t *cursor, uint32_t date_limit,
                            bool include_live, log_point_t *point)
{
    if (cursor == NULL || point == NULL || type > 3u) return false;
    if (type != LOG_RECORD_ALERT && (period < 60u || period % 60u || period > LOG_QUERY_MAX_PERIOD_SECONDS)) return false;
    uint32_t uptime = 0u, reference = 0u;
    bool valid = false;
    (void)board_rtc_log_time(&uptime, &reference, &valid);
    if (!valid) reference = uptime;
    if (memory == BOARD_LOG_MEMORY_OFF) memory = board_display_log_source();
    uint16_t backend_skip = cursor->skip;
    log_stored_t current_alert;
    if (type == LOG_RECORD_ALERT && include_live && backend_skip &&
        log_writer_live(LOG_RECORD_ALERT, &current_alert) &&
        current_alert.epoch == cursor->epoch && current_alert.time == cursor->time) --backend_skip;
    log_stored_t best = {0}, candidate;
    bool found = false, live_point = false;
    uint8_t first_kind = type == LOG_RECORD_ALERT ? LOG_RECORD_ALERT :
        (period >= 86400u ? LOG_RECORD_DAY :
         (period >= 3600u ? LOG_RECORD_HOUR : LOG_RECORD_MINUTE));
    uint8_t last_kind = first_kind;
    for (uint8_t kind = first_kind; kind <= last_kind; ++kind) {
        bool got = memory == BOARD_LOG_MEMORY_SPI ?
            spi_log_find((log_record_kind_t)kind, cursor->epoch, cursor->time,
                backend_skip, type == LOG_RECORD_ALERT, date_limit, reference, &candidate) :
            log_eeprom_find((log_record_kind_t)kind, cursor->epoch, cursor->time,
                backend_skip, type == LOG_RECORD_ALERT, date_limit, reference, &candidate);
        if (got && (!date_limit || (candidate.year & LOG_YEAR_VALID)) &&
            (!found || newer(&candidate, &best))) {
            best = candidate; found = true;
        }
    }
    log_record_kind_t live_kind = type == LOG_RECORD_ALERT ? LOG_RECORD_ALERT : LOG_RECORD_MINUTE;
    if (include_live && log_writer_live(live_kind, &candidate) &&
        (!date_limit || ((candidate.year & LOG_YEAR_VALID) && candidate.time <= date_limit)) &&
        (candidate.epoch < cursor->epoch ||
         (candidate.epoch == cursor->epoch && (candidate.time < cursor->time ||
          (type == LOG_RECORD_ALERT && candidate.time == cursor->time && cursor->skip == 0u)))) &&
        (!found || newer(&candidate, &best) ||
         (candidate.epoch == best.epoch && candidate.time == best.time))) {
        best = candidate; found = true; live_point = true;
    }
    if (!found) return false;
    if (type == LOG_RECORD_ALERT) {
        *point = (log_point_t){
            .period_start = best.time, .period_seconds = best.record.duration_seconds,
            .count = best.record.count, .maximum_cpm = best.record.maximum_cpm,
            .average_cpm = log_record_average_cpm(best.record.count, best.record.duration_seconds),
            .covered_seconds = best.record.duration_seconds, .epoch = best.epoch, .id = best.id,
            .calendar_valid = (best.year & LOG_YEAR_VALID) != 0u,
            .available = true, .live = live_point
        };
        cursor->skip = best.epoch == cursor->epoch && best.time == cursor->time ?
            (uint16_t)(cursor->skip + 1u) : 1u;
    } else if (!live_point || period == 60u) {
        uint32_t start = best.time - best.time % period;
        uint32_t coverage = best.record.covered_seconds;
        uint32_t count = best.record.count;
        uint32_t maximum = best.record.maximum_cpm;
        if (live_point && coverage == 0u && best.record.duration_seconds == 0u) {
            count = 0u;
            maximum = 0u;
        } else if (coverage == 0u) {
            coverage = best.record.duration_seconds != 0u ?
                best.record.duration_seconds : period;
        }
        if (coverage > period) coverage = period;
        *point = (log_point_t){
            .period_start = start, .period_seconds = period,
            .count = count,
            .average_cpm = log_record_average_cpm(count, coverage),
            .maximum_cpm = maximum,
            .covered_seconds = coverage, .epoch = best.epoch, .id = best.id,
            .calendar_valid = (best.year & LOG_YEAR_VALID) != 0u,
            .available = true, .partial = coverage != period,
            .live = live_point
        };
        cursor->skip = 0u;
        best.time = start;
    } else {
        uint32_t start = best.time - best.time % period;
        history_reader_t reader = {{(best.year & LOG_YEAR_VALID) != 0u}, memory, include_live, date_limit};
        if (!log_query_aggregate(read_record, &reader, best.epoch, start, period, point)) return false;
        point->calendar_valid = reader.time.calendar_valid;
        cursor->skip = 0u;
        best.time = start;
    }
    cursor->time = best.time;
    cursor->epoch = best.epoch;
    return true;
}

bool log_history_next(uint8_t type, uint32_t period, log_cursor_t *cursor,
                       uint32_t date_limit, log_point_t *point)
{
    return log_history_next_from(board_display_log_memory(), type, period,
        cursor, date_limit, true, point);
}

uint32_t log_history_count(uint8_t type)
{
    if (type > 3u) return 0u;
    uint8_t memory = board_display_log_source();
    uint32_t count = memory == BOARD_LOG_MEMORY_SPI ?
        spi_log_count((log_record_kind_t)type) : log_eeprom_count((log_record_kind_t)type);
    return count;
}
