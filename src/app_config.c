#include "app_config.h"

static bool brightness_is_supported(uint8_t percent)
{
    switch (percent) {
    case 1u:
    case 3u:
    case 5u:
    case 10u:
    case 30u:
    case 50u:
    case 70u:
    case 100u:
        return true;
    default:
        return false;
    }
}

void app_config_defaults(app_config_t *config)
{
    if (config == 0) {
        return;
    }

    config->schema_version = APP_CONFIG_SCHEMA_VERSION;
    config->dose_factor_uSv_h_per_cps_x1e6 = 0u;
    config->screen_timeout_s = 15u;
    config->brightness_percent = 30u;
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
           brightness_is_supported(config->brightness_percent) &&
           config->significant_digits >= 1u &&
           config->significant_digits <= 9u &&
           config->click_mode <= APP_CLICK_ALWAYS;
}
