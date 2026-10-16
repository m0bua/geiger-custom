#include "app.h"

void app_init(app_t *app)
{
    if (app == 0) {
        return;
    }
    app_config_defaults(&app->config);
    measurement_init(&app->measurement);
    app->uptime_seconds = 0u;
}

void app_tick_1s(app_t *app, uint32_t *cps, uint32_t *cpm)
{
    if (app == 0) {
        if (cps != 0) *cps = 0u;
        if (cpm != 0) *cpm = 0u;
        return;
    }
    if (app->uptime_seconds != UINT32_MAX) {
        ++app->uptime_seconds;
    }
    measurement_tick_1s(&app->measurement, cps, cpm);
}
