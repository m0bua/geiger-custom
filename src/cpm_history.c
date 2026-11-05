#include "cpm_history.h"

#include <stddef.h>

#define CPM_HISTORY_SAMPLE_BITS 18u
#define CPM_HISTORY_SAMPLE_MASK 0x3FFFFu
#define CPM_HISTORY_TOTAL_SAMPLES (CPM_HISTORY_FAST_CAPACITY + \
    CPM_HISTORY_MEDIUM_CAPACITY + CPM_HISTORY_SLOW_CAPACITY)
#define CPM_HISTORY_STORAGE_BYTES ((CPM_HISTORY_TOTAL_SAMPLES * \
    CPM_HISTORY_SAMPLE_BITS + 7u) / 8u)

static const uint16_t cpm_tier_offsets[CPM_HISTORY_TIER_COUNT] = {
    0u, CPM_HISTORY_FAST_CAPACITY,
    CPM_HISTORY_FAST_CAPACITY + CPM_HISTORY_MEDIUM_CAPACITY
};
static const uint8_t cpm_tier_capacities[CPM_HISTORY_TIER_COUNT] = {
    CPM_HISTORY_FAST_CAPACITY, CPM_HISTORY_MEDIUM_CAPACITY,
    CPM_HISTORY_SLOW_CAPACITY
};
static uint8_t cpm_samples[CPM_HISTORY_STORAGE_BYTES];
static uint8_t cpm_pulse_flags[(CPM_HISTORY_TOTAL_SAMPLES + 7u) / 8u];
static uint8_t cpm_head[CPM_HISTORY_TIER_COUNT];
static uint8_t cpm_count[CPM_HISTORY_TIER_COUNT];
static uint8_t cpm_tick_phase;

static void cpm_history_store(uint16_t sample_index, uint32_t cpm)
{
    const uint32_t bit_index = (uint32_t)sample_index * CPM_HISTORY_SAMPLE_BITS;
    const uint16_t byte_index = (uint16_t)(bit_index >> 3u);
    /* 18-bit samples begin at even bit offsets, so three bytes always suffice. */
    const uint8_t shift = (uint8_t)(bit_index & 7u);
    uint32_t packed = (uint32_t)cpm_samples[byte_index] |
        ((uint32_t)cpm_samples[byte_index + 1u] << 8u) |
        ((uint32_t)cpm_samples[byte_index + 2u] << 16u);
    const uint32_t mask = CPM_HISTORY_SAMPLE_MASK << shift;
    packed = (packed & ~mask) | ((cpm & CPM_HISTORY_SAMPLE_MASK) << shift);
    cpm_samples[byte_index] = (uint8_t)packed;
    cpm_samples[byte_index + 1u] = (uint8_t)(packed >> 8u);
    cpm_samples[byte_index + 2u] = (uint8_t)(packed >> 16u);
}

static uint32_t cpm_history_load(uint16_t sample_index)
{
    const uint32_t bit_index = (uint32_t)sample_index * CPM_HISTORY_SAMPLE_BITS;
    const uint16_t byte_index = (uint16_t)(bit_index >> 3u);
    const uint8_t shift = (uint8_t)(bit_index & 7u);
    uint32_t packed = (uint32_t)cpm_samples[byte_index] |
        ((uint32_t)cpm_samples[byte_index + 1u] << 8u) |
        ((uint32_t)cpm_samples[byte_index + 2u] << 16u);
    return (packed >> shift) & CPM_HISTORY_SAMPLE_MASK;
}

static void cpm_history_store_pulse_flag(uint16_t sample_index, bool present)
{
    const uint16_t byte_index = sample_index >> 3u;
    const uint8_t mask = (uint8_t)(1u << (sample_index & 7u));
    if (present) cpm_pulse_flags[byte_index] |= mask;
    else cpm_pulse_flags[byte_index] &= (uint8_t)~mask;
}

static bool cpm_history_load_pulse_flag(uint16_t sample_index)
{
    return (cpm_pulse_flags[sample_index >> 3u] &
        (uint8_t)(1u << (sample_index & 7u))) != 0u;
}

void cpm_history_init(void)
{
    for (uint8_t tier = 0u; tier < CPM_HISTORY_TIER_COUNT; ++tier) {
        cpm_head[tier] = 0u;
        cpm_count[tier] = 0u;
    }
    cpm_tick_phase = 0u;
}

void cpm_history_push(uint32_t cpm, uint32_t pulses)
{
    if (cpm > CPM_HISTORY_SAMPLE_MASK) cpm = CPM_HISTORY_SAMPLE_MASK;
    const uint8_t fast = cpm_head[CPM_HISTORY_FAST];
    cpm_history_store((uint16_t)(cpm_tier_offsets[CPM_HISTORY_FAST] + fast), cpm);
    cpm_history_store_pulse_flag(
        (uint16_t)(cpm_tier_offsets[CPM_HISTORY_FAST] + fast), pulses != 0u);
    cpm_head[CPM_HISTORY_FAST] = (uint8_t)((fast + 1u) % cpm_tier_capacities[CPM_HISTORY_FAST]);
    if (cpm_count[CPM_HISTORY_FAST] < cpm_tier_capacities[CPM_HISTORY_FAST]) ++cpm_count[CPM_HISTORY_FAST];
    if (cpm_tick_phase % 5u == 0u) {
        const uint8_t index = cpm_head[CPM_HISTORY_MEDIUM];
        cpm_history_store((uint16_t)(cpm_tier_offsets[CPM_HISTORY_MEDIUM] + index), cpm);
        cpm_history_store_pulse_flag(
            (uint16_t)(cpm_tier_offsets[CPM_HISTORY_MEDIUM] + index),
            pulses != 0u);
        cpm_head[CPM_HISTORY_MEDIUM] = (uint8_t)((index + 1u) % cpm_tier_capacities[CPM_HISTORY_MEDIUM]);
        if (cpm_count[CPM_HISTORY_MEDIUM] < cpm_tier_capacities[CPM_HISTORY_MEDIUM]) ++cpm_count[CPM_HISTORY_MEDIUM];
    }
    if (cpm_tick_phase % 18u == 0u) {
        const uint8_t index = cpm_head[CPM_HISTORY_SLOW];
        cpm_history_store((uint16_t)(cpm_tier_offsets[CPM_HISTORY_SLOW] + index), cpm);
        cpm_history_store_pulse_flag(
            (uint16_t)(cpm_tier_offsets[CPM_HISTORY_SLOW] + index),
            pulses != 0u);
        cpm_head[CPM_HISTORY_SLOW] = (uint8_t)((index + 1u) % cpm_tier_capacities[CPM_HISTORY_SLOW]);
        if (cpm_count[CPM_HISTORY_SLOW] < cpm_tier_capacities[CPM_HISTORY_SLOW]) ++cpm_count[CPM_HISTORY_SLOW];
    }
    cpm_tick_phase = (uint8_t)((cpm_tick_phase + 1u) % 90u);
}

uint8_t cpm_history_count(uint8_t tier)
{
    return tier < CPM_HISTORY_TIER_COUNT ? cpm_count[tier] : 0u;
}

bool cpm_history_get(uint8_t tier, uint8_t age, uint32_t *cpm)
{
    if (cpm == NULL || tier >= CPM_HISTORY_TIER_COUNT || age >= cpm_count[tier])
        return false;
    const uint8_t index = (uint8_t)((cpm_head[tier] +
        cpm_tier_capacities[tier] - 1u - age) % cpm_tier_capacities[tier]);
    *cpm = cpm_history_load((uint16_t)(cpm_tier_offsets[tier] + index));
    return true;
}

bool cpm_history_get_pulses(uint8_t tier, uint8_t age)
{
    if (tier >= CPM_HISTORY_TIER_COUNT || age >= cpm_count[tier]) return false;
    const uint8_t index = (uint8_t)((cpm_head[tier] +
        cpm_tier_capacities[tier] - 1u - age) % cpm_tier_capacities[tier]);
    return cpm_history_load_pulse_flag(
        (uint16_t)(cpm_tier_offsets[tier] + index));
}
