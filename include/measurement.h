#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <stdint.h>

typedef struct {
    uint32_t second_pulses;
    uint32_t second_index;
    uint32_t minute_pulses[60];
    uint8_t minute_head;
} measurement_t;

void measurement_init(measurement_t *measurement);
/* Call from the detector capture path after electrical polarity/filtering has
 * been validated. Counter saturates rather than wrapping. */
void measurement_pulse_isr(measurement_t *measurement);
/* Call once per elapsed second; returns current CPS and rolling CPM. The board
 * adapter must briefly mask detector interrupts while this consumes and clears
 * second_pulses, so an edge cannot be lost during the read/reset. */
void measurement_tick_1s(measurement_t *measurement,
                         uint32_t *cps,
                         uint32_t *cpm);

#endif
