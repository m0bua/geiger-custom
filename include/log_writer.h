#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include <stdbool.h>
#include <stdint.h>
#include "log_record.h"

/* Starts persistent log production. The storage backend follows LOG MEMORY. */
bool log_writer_init(void);

/* Feed one second of detector data into live and persistent records. */
void log_writer_second(uint32_t pulses, uint32_t rate_cpm,
                       uint32_t uptime_seconds, uint32_t calendar_seconds,
                       bool calendar_valid);
void log_writer_hv_disabled(void);
uint32_t log_writer_current_rate_cpm(void);

bool log_writer_fault(void);
uint8_t log_writer_fault_reason(void);
bool log_writer_live(log_record_kind_t kind, log_stored_t *item);
bool log_writer_change_memory(uint8_t target);
void log_writer_memory_committed(uint8_t target);
#endif
