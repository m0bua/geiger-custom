#include "app.h"

void app_init(app_t *app)
{
    if (app == 0) {
        return;
    }
    app_config_defaults(&app->config);
    measurement_init(&app->measurement);
    rgb_cycle_init(&app->rgb_cycle);
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

uint8_t app_rgb_button_step(app_t *app, rgb_direction_t direction)
{
    if (app == 0) {
        return 0u;
    }
    return rgb_color_channels(rgb_cycle_step(&app->rgb_cycle, direction));
}
