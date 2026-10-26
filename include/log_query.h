#ifndef LOG_QUERY_H
#define LOG_QUERY_H

#include "log_record.h"

#define LOG_QUERY_MAX_PERIOD_SECONDS (7u * 86400u)

typedef struct {
    uint32_t period_start;
    uint32_t period_seconds;
    uint32_t count;
    uint32_t average_cpm;
    uint32_t maximum_cpm;
    uint32_t covered_seconds;
    uint16_t epoch;
    uint16_t id;
    bool calendar_valid;
    bool available;
    bool partial;
    bool live;
} log_point_t;

typedef struct {
    bool calendar_valid;
} log_query_time_context_t;

/* Fetch an exact stored interval. A live minute may be returned by the
 * callback when the requested interval is the current minute. */
typedef bool (*log_query_read_fn)(void *context, log_record_kind_t kind,
                                  uint16_t epoch, uint32_t period_start,
                                  log_record_t *record, bool *live);

/* Return n newest-first points, each covering m seconds. The reader and
 * aggregator are shared by every display, application and protocol caller. */
bool log_query_read(log_query_read_fn read_record, void *context,
                    uint16_t epoch, uint32_t newest_time, uint8_t n,
                    uint32_t m, log_point_t *points);
bool log_query_aggregate(log_query_read_fn read_record, void *context,
                         uint16_t epoch, uint32_t period_start,
                         uint32_t period_seconds, log_point_t *point);

#endif
