#ifndef SECOND_LOG_H
#define SECOND_LOG_H

#include <stdbool.h>
#include <stdint.h>

#define SECOND_LOG_CAPACITY 90u

void second_log_init(void);
void second_log_reset_rate_window(void);
uint32_t second_log_add(uint32_t pulses);
uint8_t second_log_count(void);
bool second_log_get(uint8_t age, uint32_t *pulses);

#endif
