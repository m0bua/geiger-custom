#include "board.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    uint32_t next_sample;
    uint32_t next_adc_sample;
    uint32_t inactivity_deadline;
    uint32_t event_count = 0u;
    uint32_t previous_minute_sample = 0u;
    uint32_t minute_total = 0u;
    uint32_t minute_bins[60] = {0u};
    uint32_t next_minute_tick;
    uint8_t minute_head = 0u;
    uint32_t led_blink_deadline = 0u;
    bool backlight_on = false;
    bool event_led_on = false;

    board_init();
    board_adc_init();
    board_rgb_set(0u);
    board_rgb_set_brightness(30u);
    board_lcd_backlight_set_brightness(10u);
    board_lcd_backlight_set(false);
    board_display_init();

    next_sample = board_millis;
    next_adc_sample = board_millis;
    next_minute_tick = board_millis + 1000u;
    inactivity_deadline = board_millis + 10000u;

    for (;;) {
        board_display_service();
        uint32_t now = board_millis;

        if ((uint32_t)(now - next_sample) >= 10u) {
            const uint8_t events = board_buttons_sample_10ms();
            const uint32_t detected_pulses = board_detector_count();
            const uint8_t button_events = BOARD_BUTTON_LEFT_DOWN |
                BOARD_BUTTON_RIGHT_DOWN | BOARD_BUTTON_LEFT |
                BOARD_BUTTON_RIGHT | BOARD_BUTTON_LEFT_LONG |
                BOARD_BUTTON_RIGHT_LONG;

            next_sample += 10u;
            if ((events & button_events) != 0u) {
                board_lcd_backlight_set(true);
                backlight_on = true;
                inactivity_deadline = now + 10000u;
            }

            if (detected_pulses != event_count) {
                event_count = detected_pulses;
                board_rgb_set(2u); /* Green event flash; independent of LCD. */
                event_led_on = true;
                led_blink_deadline = now + 100u;
            }

            while ((int32_t)(now - next_minute_tick) >= 0) {
                const uint32_t second_pulses =
                    detected_pulses - previous_minute_sample;

                previous_minute_sample = detected_pulses;
                minute_total -= minute_bins[minute_head];
                minute_bins[minute_head] = second_pulses;
                minute_total += second_pulses;
                minute_head = (uint8_t)((minute_head + 1u) % 60u);
                board_display_set_cpm(minute_total);
                next_minute_tick += 1000u;
            }

            if (backlight_on && (int32_t)(now - inactivity_deadline) >= 0) {
                board_lcd_backlight_set(false);
                backlight_on = false;
            }

            if (event_led_on &&
                (int32_t)(now - led_blink_deadline) >= 0) {
                board_rgb_set(0u);
                event_led_on = false;
            }
        }

        if ((int32_t)(now - next_adc_sample) >= 0) {
            const uint16_t channel2 = board_adc_read(2u);
            const uint16_t channel8 = board_adc_read(8u);
            const uint16_t channel9 = board_adc_read(9u);

            board_display_set_adc(channel2, channel8, channel9);
            next_adc_sample += 1000u;
        }
    }
}
