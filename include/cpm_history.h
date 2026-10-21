#ifndef CPM_HISTORY_H
#define CPM_HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#define CPM_HISTORY_CAPACITY 180u
#define CPM_HISTORY_BYTES_PER_SAMPLE 3u

typedef struct {
    uint8_t samples[CPM_HISTORY_CAPACITY * CPM_HISTORY_BYTES_PER_SAMPLE];
    uint8_t head;
    uint8_t count;
} cpm_history_t;

void cpm_history_init(cpm_history_t *history);
void cpm_history_push(cpm_history_t *history, uint32_t cpm);
uint8_t cpm_history_count(const cpm_history_t *history);
/* Read by age: age 0 is the newest sample; false means unavailable. */
bool cpm_history_get(const cpm_history_t *history, uint8_t age,
                     uint32_t *cpm);

#endif
