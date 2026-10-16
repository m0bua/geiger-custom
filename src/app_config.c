#include "app_config.h"

void app_config_defaults(app_config_t *config)
{
    if (config == 0) {
        return;
    }

    config->schema_version = APP_CONFIG_SCHEMA_VERSION;
    config->dose_factor_uSv_h_per_cps_x1e6 = 0u;
    config->screen_timeout_s = 15u;
    config->brightness_percent = 70u;
    config->significant_digits = 3u;
    config->click_mode = APP_CLICK_SCREEN_ON;
    config->hide_leading_zero = false;
    config->auto_scale_units = true;
}

bool app_config_is_valid(const app_config_t *config)
{
    return config != 0 &&
           config->schema_version == APP_CONFIG_SCHEMA_VERSION &&
           config->screen_timeout_s <= 90u &&
           config->brightness_percent >= 10u &&
           config->brightness_percent <= 100u &&
           config->significant_digits >= 1u &&
           config->significant_digits <= 9u &&
           config->click_mode <= APP_CLICK_ALWAYS;
}
