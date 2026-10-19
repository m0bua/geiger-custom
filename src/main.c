#include "app.h"
#include "board.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    app_t app;
    uint32_t next_sample = 0u;
    uint32_t inactivity_deadline;
    uint8_t rgb_brightness_percent;
    uint8_t backlight_brightness_percent = 100u;
    bool backlight_on = false;
    bool rgb_on = false;
    bool rgb_sleeping = false;
    uint8_t suppress_short_mask = 0u;

    board_init();
    app_init(&app); /* logical RGB state starts OFF */
    rgb_brightness_percent = app.config.brightness_percent;
    board_rgb_set_brightness(rgb_brightness_percent);
    board_rgb_set(0u);
    /* Initialize the LCD controller while its backlight remains off so the
     * first button press only needs to update the visible color. */
    board_display_init();
    /* Start button polling and idle timing from the configured hardware tick. */
    next_sample = board_millis;
    inactivity_deadline = board_millis + 3000u;

    for (;;) {
        board_display_service();
        uint32_t now = board_millis;
        if ((uint32_t)(now - next_sample) >= 10u) {
            uint8_t events;
            bool display_dirty = false;
            next_sample += 10u;
            events = board_buttons_sample_10ms();

            /* Wake the saved color on press. Suppress that press's short
             * release event so waking does not also advance the color cycle. */
            if (rgb_sleeping && app.rgb_cycle.color != RGB_COLOR_OFF) {
                if ((events & BOARD_BUTTON_LEFT_DOWN) != 0u) {
                    board_rgb_set(rgb_color_channels(app.rgb_cycle.color));
                    rgb_on = true;
                    rgb_sleeping = false;
                    suppress_short_mask |= BOARD_BUTTON_LEFT;
                    display_dirty = true;
                }
                if ((events & BOARD_BUTTON_RIGHT_DOWN) != 0u) {
                    board_rgb_set(rgb_color_channels(app.rgb_cycle.color));
                    rgb_on = true;
                    rgb_sleeping = false;
                    suppress_short_mask |= BOARD_BUTTON_RIGHT;
                    display_dirty = true;
                }
            }

            if ((events & (BOARD_BUTTON_LEFT_DOWN | BOARD_BUTTON_RIGHT_DOWN |
                           BOARD_BUTTON_LEFT | BOARD_BUTTON_RIGHT |
                           BOARD_BUTTON_LEFT_LONG | BOARD_BUTTON_RIGHT_LONG)) != 0u) {
                board_lcd_backlight_set(true);
                backlight_on = true;
                inactivity_deadline = now + 3000u;
            }
            if ((events & BOARD_BUTTON_RIGHT) != 0u &&
                (suppress_short_mask & BOARD_BUTTON_RIGHT) != 0u) {
                suppress_short_mask &= (uint8_t)~BOARD_BUTTON_RIGHT;
            } else if ((events & BOARD_BUTTON_RIGHT) != 0u) {
                board_rgb_set(app_rgb_button_step(&app, RGB_STEP_FORWARD));
                rgb_on = app.rgb_cycle.color != RGB_COLOR_OFF;
                display_dirty = true;
            }
            if ((events & BOARD_BUTTON_LEFT) != 0u &&
                (suppress_short_mask & BOARD_BUTTON_LEFT) != 0u) {
                suppress_short_mask &= (uint8_t)~BOARD_BUTTON_LEFT;
            } else if ((events & BOARD_BUTTON_LEFT) != 0u) {
                board_rgb_set(app_rgb_button_step(&app, RGB_STEP_BACKWARD));
                rgb_on = app.rgb_cycle.color != RGB_COLOR_OFF;
                display_dirty = true;
            }
            if ((events & BOARD_BUTTON_RIGHT_LONG) != 0u) {
                suppress_short_mask &= (uint8_t)~BOARD_BUTTON_RIGHT;
            }
            if ((events & BOARD_BUTTON_LEFT_LONG) != 0u) {
                suppress_short_mask &= (uint8_t)~BOARD_BUTTON_LEFT;
            }
            if ((events & BOARD_BUTTON_RIGHT_LONG) != 0u) {
                if (backlight_brightness_percent < 100u) {
                    ++backlight_brightness_percent;
                    board_lcd_backlight_set_brightness(backlight_brightness_percent);
                }
                if (rgb_brightness_percent < 100u) {
                    ++rgb_brightness_percent;
                    board_rgb_set_brightness(rgb_brightness_percent);
                }
            }
            if ((events & BOARD_BUTTON_LEFT_LONG) != 0u) {
                if (backlight_brightness_percent > 0u) {
                    --backlight_brightness_percent;
                    board_lcd_backlight_set_brightness(backlight_brightness_percent);
                }
                if (rgb_brightness_percent > 0u) {
                    --rgb_brightness_percent;
                    board_rgb_set_brightness(rgb_brightness_percent);
                }
            }
            if (display_dirty) {
                board_display_set_color(rgb_color_channels(app.rgb_cycle.color));
                /* Apply RGB/button state first; LCD startup is secondary. */
                board_display_init();
            }
            if ((backlight_on || rgb_on) &&
                (int32_t)(now - inactivity_deadline) >= 0) {
                if (backlight_on && board_display_busy()) {
                    /* Keep the panel visible until its current frame is sent;
                     * start the normal idle timeout after rendering completes. */
                    inactivity_deadline = now + 3000u;
                    continue;
                }
                if (backlight_on) {
                    board_lcd_backlight_set(false);
                    backlight_on = false;
                }
                if (rgb_on) {
                    board_rgb_set(0u);
                    rgb_on = false;
                    rgb_sleeping = app.rgb_cycle.color != RGB_COLOR_OFF;
                }
            }
        }
    }
}
