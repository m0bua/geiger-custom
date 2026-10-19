#include "board.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    uint32_t next_sample;
    uint32_t inactivity_deadline;
    bool backlight_on = false;

    board_init();
    board_rgb_set(0u);
    board_rgb_set_brightness(30u);
    board_lcd_backlight_set_brightness(10u);
    board_lcd_backlight_set(false);
    board_display_init();

    next_sample = board_millis;
    inactivity_deadline = board_millis + 3000u;

    for (;;) {
        board_display_service();
        uint32_t now = board_millis;

        if ((uint32_t)(now - next_sample) >= 10u) {
            const uint8_t events = board_buttons_sample_10ms();
            const uint8_t button_events = BOARD_BUTTON_LEFT_DOWN |
                BOARD_BUTTON_RIGHT_DOWN | BOARD_BUTTON_LEFT |
                BOARD_BUTTON_RIGHT | BOARD_BUTTON_LEFT_LONG |
                BOARD_BUTTON_RIGHT_LONG;

            next_sample += 10u;
            if ((events & button_events) != 0u) {
                board_lcd_backlight_set(true);
                backlight_on = true;
                inactivity_deadline = now + 3000u;
            }

            if ((events & (BOARD_BUTTON_LEFT | BOARD_BUTTON_RIGHT)) != 0u) {
                board_display_test_next();
            }

            if (backlight_on && (int32_t)(now - inactivity_deadline) >= 0) {
                board_lcd_backlight_set(false);
                backlight_on = false;
            }
        }
    }
}
