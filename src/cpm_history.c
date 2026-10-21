#include "cpm_history.h"

#include <stddef.h>

void cpm_history_init(cpm_history_t *history)
{
    if (history == NULL) {
        return;
    }

    for (size_t i = 0u; i < sizeof(history->samples); ++i) {
        history->samples[i] = 0u;
    }
    history->head = 0u;
    history->count = 0u;
}

void cpm_history_push(cpm_history_t *history, uint32_t cpm)
{
    uint8_t *sample;

    if (history == NULL) {
        return;
    }

    if (cpm > 0xFFFFFFu) {
        cpm = 0xFFFFFFu;
    }

    sample = &history->samples[(uint16_t)history->head *
        CPM_HISTORY_BYTES_PER_SAMPLE];
    sample[0] = (uint8_t)cpm;
    sample[1] = (uint8_t)(cpm >> 8u);
    sample[2] = (uint8_t)(cpm >> 16u);

    history->head = (uint8_t)((history->head + 1u) % CPM_HISTORY_CAPACITY);
    if (history->count < CPM_HISTORY_CAPACITY) {
        ++history->count;
    }
}

uint8_t cpm_history_count(const cpm_history_t *history)
{
    return history == NULL ? 0u : history->count;
}

bool cpm_history_get(const cpm_history_t *history, uint8_t age,
                     uint32_t *cpm)
{
    uint8_t index;
    uint16_t offset;

    if (history == NULL || cpm == NULL || age >= history->count) {
        return false;
    }

    index = (uint8_t)((history->head + CPM_HISTORY_CAPACITY - 1u - age) %
        CPM_HISTORY_CAPACITY);
    offset = (uint16_t)index * CPM_HISTORY_BYTES_PER_SAMPLE;
    *cpm = (uint32_t)history->samples[offset] |
        ((uint32_t)history->samples[offset + 1u] << 8u) |
        ((uint32_t)history->samples[offset + 2u] << 16u);
    return true;
}
