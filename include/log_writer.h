#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include <stdbool.h>
#include <stdint.h>

/* Starts persistent log production. The storage backend follows LOG MEMORY. */
bool log_writer_init(void);

/* Feed one second of detector data. Only complete calendar minutes are saved. */
void log_writer_second(uint32_t pulses, uint32_t cpm,
                       uint32_t uptime_seconds, uint32_t calendar_seconds,
                       bool calendar_valid);

#endif
