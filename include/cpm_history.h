#ifndef CPM_HISTORY_H
#define CPM_HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#define CPM_HISTORY_FAST 0u
#define CPM_HISTORY_MEDIUM 1u
#define CPM_HISTORY_SLOW 2u
#define CPM_HISTORY_TIER_COUNT 3u

#define CPM_HISTORY_FAST_CAPACITY 201u
#define CPM_HISTORY_MEDIUM_CAPACITY 121u
#define CPM_HISTORY_SLOW_CAPACITY 224u

void cpm_history_init(void);
/* Samples are captured into 30 s, 90 s, and 10 min rings. */
void cpm_history_push(uint32_t cpm);
uint8_t cpm_history_count(uint8_t tier);
/* Read by age: age 0 is the newest sample; false means unavailable. */
bool cpm_history_get(uint8_t tier, uint8_t age, uint32_t *cpm);

#endif
