#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define APP_CONFIG_SCHEMA_VERSION 1u

typedef enum {
    APP_UNIT_SV = 0,
    APP_UNIT_COUNT = 1,
    APP_UNIT_R = 2
} app_unit_family_t;

typedef enum {
    APP_CLICK_OFF = 0,
    APP_CLICK_SCREEN_ON = 1,
    APP_CLICK_ALWAYS = 2
} app_click_mode_t;

/* Calibration is represented in micro-sieverts per hour per CPS, scaled by
 * 1e6. The factory value is intentionally unset until verified. */
typedef struct {
    uint32_t schema_version;
    uint32_t dose_factor_uSv_h_per_cps_x1e6;
    uint16_t screen_timeout_s;
    uint8_t brightness_percent;
    uint8_t significant_digits;
    app_click_mode_t click_mode;
    bool hide_leading_zero;
    bool auto_scale_units;
} app_config_t;

/* Safe software defaults only; board outputs must separately be initialized
 * to measured safe states by the board layer. */
void app_config_defaults(app_config_t *config);
bool app_config_is_valid(const app_config_t *config);

#endif
