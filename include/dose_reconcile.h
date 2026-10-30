#ifndef DOSE_RECONCILE_H
#define DOSE_RECONCILE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DOSE_RECONCILE_IDLE = 0u,
    DOSE_RECONCILE_PENDING,
    DOSE_RECONCILE_COMPLETE,
    DOSE_RECONCILE_FAILED
} dose_reconcile_result_t;

void dose_reconcile_begin(uint8_t memory, uint16_t day_key,
                          uint32_t day_start, uint32_t day_pulse_snapshot);
dose_reconcile_result_t dose_reconcile_step(uint8_t memory,
                          uint16_t day_key, uint32_t live_day_pulses,
                          uint32_t *day_pulses, uint32_t *total_pulses);
void dose_reconcile_cancel(void);
void dose_reconcile_invalidate_history(void);

#endif
