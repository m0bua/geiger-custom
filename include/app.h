#ifndef APP_H
#define APP_H

#include "app_config.h"
#include "measurement.h"
#include "rgb_cycle.h"

typedef struct {
    app_config_t config;
    measurement_t measurement;
    rgb_cycle_t rgb_cycle;
    uint32_t uptime_seconds;
} app_t;

void app_init(app_t *app);
/* Portable one-second task hook. Hardware scheduler and display integration
 * belong to the board layer. */
void app_tick_1s(app_t *app, uint32_t *cps, uint32_t *cpm);
/* Advance the logical LED state; return R/G/B channel mask for the board
 * adapter. Starts at off. */
uint8_t app_rgb_button_step(app_t *app, rgb_direction_t direction);

#endif
