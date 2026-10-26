#include "second_log.h"

static uint32_t samples[SECOND_LOG_CAPACITY];
static uint32_t rolling_pulses;
static uint8_t head;
static uint8_t count;
static uint8_t rate_window_count;

void second_log_init(void)
{
    for (uint8_t i = 0u; i < SECOND_LOG_CAPACITY; ++i) samples[i] = 0u;
    rolling_pulses = 0u;
    head = 0u;
    count = 0u;
    rate_window_count = 0u;
}

void second_log_reset_rate_window(void)
{
    rolling_pulses = 0u;
    rate_window_count = 0u;
}

uint32_t second_log_add(uint32_t pulses)
{
    if (rate_window_count == 60u) {
        const uint8_t oldest = (uint8_t)((head + SECOND_LOG_CAPACITY - 60u) %
            SECOND_LOG_CAPACITY);
        rolling_pulses -= samples[oldest];
    } else if (rate_window_count < 60u) {
        ++rate_window_count;
    }
    samples[head] = pulses;
    rolling_pulses += pulses;
    head = (uint8_t)((head + 1u) % SECOND_LOG_CAPACITY);
    if (count < SECOND_LOG_CAPACITY) ++count;
    return (uint32_t)(((uint64_t)rolling_pulses * 60u +
        rate_window_count / 2u) / rate_window_count);
}

uint8_t second_log_count(void)
{
    return count;
}

bool second_log_get(uint8_t age, uint32_t *pulses)
{
    if (pulses == 0 || age >= count) return false;
    const uint8_t index = (uint8_t)((head + SECOND_LOG_CAPACITY - 1u - age) %
        SECOND_LOG_CAPACITY);
    *pulses = samples[index];
    return true;
}
