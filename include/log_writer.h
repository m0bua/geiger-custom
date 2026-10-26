#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include <stdbool.h>
#include <stdint.h>
#include "log_record.h"

/* Starts persistent log production. The storage backend follows LOG MEMORY. */
bool log_writer_init(void);

/* Feed one second of detector data. Only complete calendar minutes are saved. */
void log_writer_second(uint32_t pulses, uint32_t cpm,
                       uint32_t uptime_seconds, uint32_t calendar_seconds,
                       bool calendar_valid);

bool log_writer_fault(void);
bool log_writer_live(log_record_kind_t kind, log_stored_t *item);
bool log_writer_change_memory(uint8_t target);
void log_writer_memory_committed(uint8_t target);
#endif
