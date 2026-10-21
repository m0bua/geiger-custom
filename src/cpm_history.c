#include "cpm_history.h"

#include <stddef.h>

static uint8_t cpm_samples[CPM_HISTORY_CAPACITY *
    CPM_HISTORY_BYTES_PER_SAMPLE];
static uint8_t cpm_head;
static uint8_t cpm_count;

void cpm_history_init(void)
{
    for (size_t i = 0u; i < sizeof(cpm_samples); ++i) {
        cpm_samples[i] = 0u;
    }
    cpm_head = 0u;
    cpm_count = 0u;
}

void cpm_history_push(uint32_t cpm)
{
    uint8_t *sample;

    if (cpm > 0xFFFFFFu) {
        cpm = 0xFFFFFFu;
    }

    sample = &cpm_samples[(uint16_t)cpm_head *
        CPM_HISTORY_BYTES_PER_SAMPLE];
    sample[0] = (uint8_t)cpm;
    sample[1] = (uint8_t)(cpm >> 8u);
    sample[2] = (uint8_t)(cpm >> 16u);

    cpm_head = (uint8_t)((cpm_head + 1u) % CPM_HISTORY_CAPACITY);
    if (cpm_count < CPM_HISTORY_CAPACITY) {
        ++cpm_count;
    }
}

uint8_t cpm_history_count(void)
{
    return cpm_count;
}

bool cpm_history_get(uint8_t age, uint32_t *cpm)
{
    uint8_t index;
    uint16_t offset;

    if (cpm == NULL || age >= cpm_count) {
        return false;
    }

    index = (uint8_t)((cpm_head + CPM_HISTORY_CAPACITY - 1u - age) %
        CPM_HISTORY_CAPACITY);
    offset = (uint16_t)index * CPM_HISTORY_BYTES_PER_SAMPLE;
    *cpm = (uint32_t)cpm_samples[offset] |
        ((uint32_t)cpm_samples[offset + 1u] << 8u) |
        ((uint32_t)cpm_samples[offset + 2u] << 16u);
    return true;
}
