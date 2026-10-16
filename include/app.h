#ifndef APP_H
#define APP_H

#include "app_config.h"
#include "measurement.h"

typedef struct {
    app_config_t config;
    measurement_t measurement;
    uint32_t uptime_seconds;
} app_t;

void app_init(app_t *app);
/* Portable one-second task hook. Hardware scheduler and display integration
 * belong to the board layer and are not present yet. */
void app_tick_1s(app_t *app, uint32_t *cps, uint32_t *cpm);

#endif
