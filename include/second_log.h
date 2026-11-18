#ifndef SECOND_LOG_H
#define SECOND_LOG_H

#include <stdbool.h>
#include <stdint.h>

#define SECOND_LOG_CAPACITY 90u
#define RATE_SAMPLE_CAPACITY 400u

void second_log_init(void);
void second_log_reset_rate_window(void);
void second_log_note_pulses(uint32_t pulses, uint32_t time_100ms);
uint32_t second_log_rate_cpm(uint32_t now_100ms, uint16_t target_pulses,
                             uint16_t *sample_count);
void second_log_add(uint32_t pulses);
uint8_t second_log_count(void);
bool second_log_get(uint8_t age, uint32_t *pulses);

#endif
