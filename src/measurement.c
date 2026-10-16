#include "measurement.h"

#include <stddef.h>

void measurement_init(measurement_t *measurement)
{
    if (measurement == NULL) {
        return;
    }

    measurement->second_pulses = 0u;
    measurement->second_index = 0u;
    measurement->minute_head = 0u;
    for (size_t i = 0u; i < 60u; ++i) {
        measurement->minute_pulses[i] = 0u;
    }
}

void measurement_pulse_isr(measurement_t *measurement)
{
    if (measurement == NULL) {
        return;
    }
    if (measurement->second_pulses != UINT32_MAX) {
        ++measurement->second_pulses;
    }
}

void measurement_tick_1s(measurement_t *measurement,
                         uint32_t *cps,
                         uint32_t *cpm)
{
    uint64_t sum = 0u;

    if (measurement == NULL) {
        if (cps != NULL) *cps = 0u;
        if (cpm != NULL) *cpm = 0u;
        return;
    }

    measurement->minute_pulses[measurement->minute_head] =
        measurement->second_pulses;
    if (cps != NULL) *cps = measurement->second_pulses;
    measurement->second_pulses = 0u;
    measurement->minute_head = (uint8_t)((measurement->minute_head + 1u) % 60u);
    if (measurement->second_index < 60u) ++measurement->second_index;

    for (size_t i = 0u; i < measurement->second_index; ++i) {
        sum += measurement->minute_pulses[i];
    }
    if (cpm != NULL) *cpm = (sum > UINT32_MAX) ? UINT32_MAX : (uint32_t)sum;
}
