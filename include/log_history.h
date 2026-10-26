#ifndef LOG_HISTORY_H
#define LOG_HISTORY_H

#include "log_query.h"

typedef struct {
    uint32_t time;
    uint16_t epoch;
    uint16_t skip;
} log_cursor_t;

void log_history_begin(log_cursor_t *cursor);
bool log_history_next(uint8_t type, uint32_t period, log_cursor_t *cursor,
                       uint32_t date_limit, log_point_t *point);
bool log_history_next_from(uint8_t memory, uint8_t type, uint32_t period,
                            log_cursor_t *cursor, uint32_t date_limit,
                            bool include_live, log_point_t *point);
uint32_t log_history_count(uint8_t type);

#endif
