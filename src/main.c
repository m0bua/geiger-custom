#include "app.h"
#include "board.h"

#include <stdint.h>

static const uint8_t brightness_levels[] = {1u, 3u, 5u, 10u, 30u, 50u, 70u, 100u};

static uint8_t brightness_step(uint8_t current, bool brighter)
{
    const uint8_t count = (uint8_t)(sizeof(brightness_levels) / sizeof(brightness_levels[0]));
    for (uint8_t i = 0u; i < count; ++i) {
        if (brightness_levels[i] == current) {
            if (brighter && i + 1u < count) {
                return brightness_levels[i + 1u];
            }
            if (!brighter && i > 0u) {
                return brightness_levels[i - 1u];
            }
            return current;
        }
    }
    return brightness_levels[0];
}

int main(void)
{
    app_t app;
    uint32_t next_sample = 0u;
    uint8_t brightness_percent;

    board_init();
    app_init(&app); /* logical RGB state starts OFF */
    brightness_percent = app.config.brightness_percent;
    board_rgb_set_brightness(brightness_percent);
    board_rgb_set(0u);

    for (;;) {
        uint32_t now = board_millis;
        if ((uint32_t)(now - next_sample) >= 10u) {
            uint8_t events;
            next_sample += 10u;
            events = board_buttons_sample_10ms();
            if ((events & BOARD_BUTTON_RIGHT) != 0u) {
                board_rgb_set(app_rgb_button_step(&app, RGB_STEP_FORWARD));
            }
            if ((events & BOARD_BUTTON_LEFT) != 0u) {
                board_rgb_set(app_rgb_button_step(&app, RGB_STEP_BACKWARD));
            }
            if ((events & BOARD_BUTTON_RIGHT_LONG) != 0u && brightness_percent < 100u) {
                brightness_percent = brightness_step(brightness_percent, true);
                board_rgb_set_brightness(brightness_percent);
            }
            if ((events & BOARD_BUTTON_LEFT_LONG) != 0u && brightness_percent > 1u) {
                brightness_percent = brightness_step(brightness_percent, false);
                board_rgb_set_brightness(brightness_percent);
            }
        }
    }
}
