#include "rate_display.h"
#include "board.h"

__attribute__((noinline)) void rate_display_render(uint32_t value,
    uint32_t pulses_since_start)
{
    const uint32_t rate = (uint32_t)(((uint64_t)value * 666u + 5u) / 10u);
    board_display_set_rate(rate, (value + 5u) / 10u, pulses_since_start,
        pulses_since_start >= 30u);
}

__attribute__((noinline)) bool rate_display_update(rate_display_state_t *state,
    uint32_t now, uint32_t target, uint32_t interval_ms)
{
    const uint32_t elapsed = rate_display_ticks(state->clocks, now,
        interval_ms, state->pulse);
    state->pulse = false;
    if (elapsed == UINT32_MAX) return false;
    if (target != UINT32_MAX)
        state->value = rate_display_step(state->value, target, elapsed);
    return true;
}

uint32_t rate_display_ticks(uint32_t clocks[2],
    uint32_t now, uint32_t interval_ms, bool pulse)
{
    if (now - clocks[0] < interval_ms && !(pulse && interval_ms != 100u))
        return UINT32_MAX;
    clocks[0] = now;
    const uint32_t ticks = (now - clocks[1]) / 100u;
    clocks[1] += ticks * 100u;
    return ticks;
}

__attribute__((noinline)) uint32_t rate_display_interval(bool fast, bool screen_on, bool invisible)
{
    if (fast) return 100u;
    if (screen_on) return 500u;
    return invisible ? 5000u : 1000u;
}

__attribute__((noinline)) uint32_t rate_display_step(uint32_t current,
    uint32_t target, uint32_t ticks)
{
    /* Identical 100-ms steps make decay independent of the UI interval.
     * Stop at the target, also bounding work after a long pause. */
    for (; ticks != 0u; --ticks) {
        if (current == target) break;
        const bool rising = target > current;
        const uint32_t difference = rising ? target - current : current - target;
        uint32_t step = difference >> (rising ? 3u : 5u);
        if (step == 0u) step = 1u;
        current = rising ? current + step : current - step;
    }
    return current;
}
