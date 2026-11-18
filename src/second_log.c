#include "second_log.h"

/* The detector's supported CPM ceiling is 262144 (about 4369 pulses/s). */
static uint16_t samples[SECOND_LOG_CAPACITY];
static uint8_t rate_sample_intervals[RATE_SAMPLE_CAPACITY];
static uint8_t head;
static uint8_t count;
static uint16_t rate_sample_head;
static uint16_t rate_sample_count;
static uint32_t rate_last_pulse_time;
static uint32_t rate_window_start_time;
static uint8_t rate_have_last_pulse;

void second_log_init(void)
{
    for (uint8_t i = 0u; i < SECOND_LOG_CAPACITY; ++i) samples[i] = 0u;
    head = 0u;
    count = 0u;
    rate_sample_head = 0u;
    rate_sample_count = 0u;
    rate_have_last_pulse = 0u;
    rate_window_start_time = 0u;
}

void second_log_reset_rate_window(void)
{
    rate_sample_head = 0u;
    rate_sample_count = 0u;
    rate_have_last_pulse = 0u;
    rate_window_start_time = 0u;
}

void second_log_note_pulses(uint32_t pulses, uint32_t time_100ms)
{
    if (pulses > RATE_SAMPLE_CAPACITY) pulses = RATE_SAMPLE_CAPACITY;
    if (rate_have_last_pulse &&
        time_100ms - rate_last_pulse_time > 600u) {
        rate_sample_head = 0u;
        rate_sample_count = 0u;
        rate_have_last_pulse = 0u;
    }
    while (pulses != 0u) {
        if (!rate_have_last_pulse) rate_window_start_time = time_100ms;
        const uint16_t delta = rate_have_last_pulse ?
            (uint16_t)(time_100ms - rate_last_pulse_time) : 0u;
        uint16_t quarter_seconds = (uint16_t)((delta * 2u + 2u) / 5u);
        if (quarter_seconds > UINT8_MAX) quarter_seconds = UINT8_MAX;
        rate_sample_intervals[rate_sample_head] = (uint8_t)quarter_seconds;
        rate_last_pulse_time = time_100ms;
        rate_have_last_pulse = 1u;
        if (++rate_sample_head == RATE_SAMPLE_CAPACITY) rate_sample_head = 0u;
        if (rate_sample_count < RATE_SAMPLE_CAPACITY) ++rate_sample_count;
        --pulses;
    }
}

uint32_t second_log_rate_cpm(uint32_t now_100ms, uint16_t target_pulses,
                             uint16_t *sample_count)
{
    uint16_t used = rate_sample_count;
    if (used > target_pulses) used = target_pulses;
    if (sample_count != 0) *sample_count = used;
    if (used == 0u) return 23u;
    uint16_t oldest_index = rate_sample_head >= used ?
        (uint16_t)(rate_sample_head - used) :
        (uint16_t)(RATE_SAMPLE_CAPACITY + rate_sample_head - used);
    const uint32_t since_last_100ms = now_100ms - rate_last_pulse_time;
    if (since_last_100ms > 600u) {
        if (sample_count != 0) *sample_count = 0u;
        return 0u;
    }
    uint32_t elapsed = (uint32_t)((since_last_100ms * 2u + 2u) / 5u) * 5u;
    for (uint16_t i = 1u; i < used; ++i) {
        uint16_t index = (uint16_t)(oldest_index + i);
        if (index >= RATE_SAMPLE_CAPACITY) index -= RATE_SAMPLE_CAPACITY;
        elapsed += (uint32_t)rate_sample_intervals[index] * 5u;
    }
    /* Use 50 ms units. The pseudo-interval represents one 23 CPM prior. */
    elapsed += 52u;
    uint32_t rate = ((uint32_t)used * 1200u + elapsed / 2u) / elapsed;
    if (used < target_pulses && target_pulses != 0u) {
        const uint32_t period = (uint32_t)target_pulses * 600u / 23u;
        const uint32_t elapsed_window = now_100ms - rate_window_start_time;
        if (elapsed_window < period) {
            const uint32_t filled = (uint32_t)used * 600u +
                23u * (period - elapsed_window);
            rate = (filled + period / 2u) / period;
        }
    }
    return rate > 262144u ? 262144u : rate;
}

void second_log_add(uint32_t pulses)
{
    if (pulses > UINT16_MAX) pulses = UINT16_MAX;
    samples[head] = (uint16_t)pulses;
    head = (uint8_t)((head + 1u) % SECOND_LOG_CAPACITY);
    if (count < SECOND_LOG_CAPACITY) ++count;
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
