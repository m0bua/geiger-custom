#ifndef DAILY_DOSE_H
#define DAILY_DOSE_H

#include <stdbool.h>
#include <stdint.h>

void daily_dose_init(uint16_t day_key, uint32_t fallback_count);
void daily_dose_set_day(uint16_t day_key, uint32_t fallback_count);
void daily_dose_add(uint32_t pulses);
uint32_t daily_dose_count(void);
bool daily_dose_checkpoint(void);
uint32_t daily_dose_total(void);
void daily_dose_set_total(uint32_t count);
void daily_dose_clear(void);

#endif
