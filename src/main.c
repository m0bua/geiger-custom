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
    uint32_t pulses_since_start = 0u;
    uint8_t rate_window_seconds = 0u;
    uint32_t dose_thousandths = 0u;
    uint32_t dose_fraction = 0u;
    uint32_t minute_bins[60] = {0u};
    uint32_t next_minute_tick;
    uint8_t minute_head = 0u;
    uint32_t led_blink_deadline = 0u;
    bool backlight_on = false;
    bool event_led_on = false;
    bool left_long_handled = false;
    bool right_long_handled = false;
    uint8_t wake_suppressed_buttons = 0u;

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
            uint8_t events = board_buttons_sample_10ms();
            const uint32_t detected_pulses = board_detector_count();
            const uint8_t button_events = BOARD_BUTTON_LEFT_DOWN |
                BOARD_BUTTON_RIGHT_DOWN | BOARD_BUTTON_LEFT |
                BOARD_BUTTON_RIGHT | BOARD_BUTTON_LEFT_LONG |
                BOARD_BUTTON_RIGHT_LONG;

            next_sample += 10u;
            if (!backlight_on &&
                (events & (BOARD_BUTTON_LEFT_DOWN |
                           BOARD_BUTTON_RIGHT_DOWN)) != 0u) {
                board_lcd_backlight_set(true);
                backlight_on = true;
                inactivity_deadline = now + 10000u;
                wake_suppressed_buttons |= events &
                    (BOARD_BUTTON_LEFT_DOWN | BOARD_BUTTON_RIGHT_DOWN);
                events &= (uint8_t)~button_events;
            } else if ((events & button_events) != 0u) {
                inactivity_deadline = now + 10000u;
            }
            if ((events & BOARD_BUTTON_LEFT_UP) != 0u) {
                if ((wake_suppressed_buttons & BOARD_BUTTON_LEFT_DOWN) != 0u) {
                    events &= (uint8_t)~(BOARD_BUTTON_LEFT |
                        BOARD_BUTTON_LEFT_LONG | BOARD_BUTTON_LEFT_DOWN);
                }
                wake_suppressed_buttons &= (uint8_t)~BOARD_BUTTON_LEFT_DOWN;
            }
            if ((events & BOARD_BUTTON_RIGHT_UP) != 0u) {
                if ((wake_suppressed_buttons & BOARD_BUTTON_RIGHT_DOWN) != 0u) {
                    events &= (uint8_t)~(BOARD_BUTTON_RIGHT |
                        BOARD_BUTTON_RIGHT_LONG | BOARD_BUTTON_RIGHT_DOWN);
                }
                wake_suppressed_buttons &= (uint8_t)~BOARD_BUTTON_RIGHT_DOWN;
            }
            if ((wake_suppressed_buttons & BOARD_BUTTON_LEFT_DOWN) != 0u) {
                events &= (uint8_t)~(BOARD_BUTTON_LEFT | BOARD_BUTTON_LEFT_LONG |
                                     BOARD_BUTTON_LEFT_DOWN);
            }
            if ((wake_suppressed_buttons & BOARD_BUTTON_RIGHT_DOWN) != 0u) {
                events &= (uint8_t)~(BOARD_BUTTON_RIGHT | BOARD_BUTTON_RIGHT_LONG |
                                     BOARD_BUTTON_RIGHT_DOWN);
            }
            if ((events & BOARD_BUTTON_LEFT_DOWN) != 0u) {
                left_long_handled = false;
            }
            if ((events & BOARD_BUTTON_RIGHT_DOWN) != 0u) {
                right_long_handled = false;
            }
            if ((events & BOARD_BUTTON_LEFT_LONG) != 0u &&
                !left_long_handled) {
                board_display_extra_exit();
                left_long_handled = true;
            }
            if ((events & BOARD_BUTTON_RIGHT_LONG) != 0u &&
                !right_long_handled) {
                board_display_extra_enter();
                right_long_handled = true;
            }
            if ((events & BOARD_BUTTON_RIGHT) != 0u) {
                board_display_right_short();
            }
            if ((events & BOARD_BUTTON_LEFT) != 0u) {
                board_display_next_page();
            }

            if (detected_pulses != event_count) {
                event_count = detected_pulses;
                board_rgb_set(board_display_rate_led_mask());
                event_led_on = true;
                led_blink_deadline = now + 100u;
            }

            while ((int32_t)(now - next_minute_tick) >= 0) {
                const uint32_t second_pulses =
                    detected_pulses - previous_minute_sample;

                previous_minute_sample = detected_pulses;
                if (UINT32_MAX - pulses_since_start < second_pulses) {
                    pulses_since_start = UINT32_MAX;
                } else {
                    pulses_since_start += second_pulses;
                }
                minute_total -= minute_bins[minute_head];
                minute_bins[minute_head] = second_pulses;
                minute_total += second_pulses;
                minute_head = (uint8_t)((minute_head + 1u) % 60u);
                if (rate_window_seconds < 60u) ++rate_window_seconds;
                uint32_t cpm = minute_total;
                if (rate_window_seconds < 60u) {
                    cpm = (uint32_t)(((uint64_t)minute_total * 60u +
                        rate_window_seconds / 2u) / rate_window_seconds);
                }
                /* Rate = normalized CPM * 0.00666 µSv/h. */
                const uint32_t rate_thousandths = (uint32_t)(
                    ((uint64_t)cpm * 666u + 50u) / 100u);
                board_display_set_rate(rate_thousandths, cpm,
                                       pulses_since_start >= 25u);
                dose_fraction += rate_thousandths;
                dose_thousandths += dose_fraction / 3600u;
                dose_fraction %= 3600u;
                board_display_set_dose(dose_thousandths, dose_fraction);
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
            uint16_t channel2;
            uint16_t channel8;
            uint16_t channel9;

            board_adc_read_all(&channel2, &channel8, &channel9);
            board_display_set_adc(channel2, channel8, channel9);
            next_adc_sample += 1000u;
        }
    }
}
