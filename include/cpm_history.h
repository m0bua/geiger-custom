#ifndef CPM_HISTORY_H
#define CPM_HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#define CPM_HISTORY_CAPACITY 180u
#define CPM_HISTORY_BYTES_PER_SAMPLE 3u

void cpm_history_init(void);
void cpm_history_push(uint32_t cpm);
uint8_t cpm_history_count(void);
/* Read by age: age 0 is the newest sample; false means unavailable. */
bool cpm_history_get(uint8_t age, uint32_t *cpm);

#endif
