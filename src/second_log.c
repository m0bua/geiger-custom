#include "second_log.h"

#define RATE_STARTUP_BASELINE_CPM 23u

static uint32_t samples[SECOND_LOG_CAPACITY];
static uint32_t rolling_pulses;
static uint8_t head;
static uint8_t count;
static uint8_t rate_window_count;
static uint32_t estimated_rate_cpm;

void second_log_init(void)
{
    for (uint8_t i = 0u; i < SECOND_LOG_CAPACITY; ++i) samples[i] = 0u;
    rolling_pulses = 0u;
    head = 0u;
    count = 0u;
    rate_window_count = 0u;
    estimated_rate_cpm = RATE_STARTUP_BASELINE_CPM;
}

void second_log_reset_rate_window(void)
{
    rolling_pulses = 0u;
    rate_window_count = 0u;
    estimated_rate_cpm = RATE_STARTUP_BASELINE_CPM;
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
    const uint8_t missing_seconds = (uint8_t)(60u - rate_window_count);
    const uint64_t startup_estimate_x60 = (uint64_t)rolling_pulses * 60u +
        (uint32_t)RATE_STARTUP_BASELINE_CPM * missing_seconds;
    estimated_rate_cpm = (uint32_t)((startup_estimate_x60 + 30u) / 60u);
    return (uint32_t)(((uint64_t)rolling_pulses * 60u +
        rate_window_count / 2u) / rate_window_count);
}

uint32_t second_log_rate_cpm(void)
{
    return estimated_rate_cpm;
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
