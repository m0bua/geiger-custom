#ifndef RATE_DISPLAY_H
#define RATE_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t value;
    uint32_t clocks[2];
    bool pulse;
} rate_display_state_t;

/* UINT32_MAX target preserves the separate SEARCH estimate. */
bool rate_display_update(rate_display_state_t *state, uint32_t now,
    uint32_t target, uint32_t interval_ms);

void rate_display_render(uint32_t value, uint32_t pulses_since_start);

/* First matching condition wins: fast, screen on, normal, invisible. */
uint32_t rate_display_interval(bool fast, bool screen_on, bool invisible);

/* Two clocks: refresh deadline and filter time. Returns consumed 100-ms ticks,
 * or UINT32_MAX when no refresh is due. Pulses restart economical deadlines. */
uint32_t rate_display_ticks(uint32_t clocks[2], uint32_t now,
    uint32_t interval_ms, bool pulse);

/* NORMAL estimate in tenths of CPM; elapsed time in 100-ms ticks. */
uint32_t rate_display_step(uint32_t current, uint32_t target, uint32_t ticks);

#endif
