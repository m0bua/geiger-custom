#include "board.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    uint32_t next_sample;
    uint32_t next_adc_sample;
    uint32_t inactivity_deadline;
    uint32_t screen_timeout_ms;
    uint32_t event_count = 0u;
    uint32_t previous_minute_sample = 0u;
    uint32_t minute_total = 0u;
    uint32_t pulses_since_start = 0u;
    uint32_t session_pulses_total = 0u;
    uint32_t session_elapsed_seconds = 0u;
    uint32_t session_max_rate_thousandths = 0u;
    uint32_t session_max_cpm = 0u;
    uint8_t rate_window_seconds = 0u;
    uint32_t dose_thousandths = 0u;
    uint32_t dose_fraction = 0u;
    uint32_t dose_measurement_seconds = 0u;
    uint32_t minute_bins[60] = {0u};
    uint32_t next_minute_tick;
    uint8_t minute_head = 0u;
    uint32_t led_blink_deadline = 0u;
    uint32_t light_alarm_deadline = 0u;
    uint32_t startup_led_deadline;
    bool backlight_on = false;
    bool screen_alarm_active = false;
    bool light_alarm_active = false;
    bool sound_alarm_active = false;
    bool alarms_acknowledged = false;
    bool light_alarm_on = false;
    bool event_led_on = false;
    bool startup_led_active = true;
    bool hv_enabled;
    bool left_long_handled = false;
    bool right_long_handled = false;
    uint8_t wake_suppressed_buttons = 0u;
    uint8_t alarm_suppressed_buttons = 0u;

    board_init();
    board_adc_init();
    board_rgb_set(0u);
    board_rgb_set_brightness(30u);
    board_rgb_set(7u);
    startup_led_deadline = board_millis + 1000u;
    board_lcd_backlight_set_brightness(10u);
    board_lcd_backlight_set(false);
    board_display_init();
    screen_alarm_active = board_display_screen_alarm_active();
    sound_alarm_active = board_display_sound_alarm_active();
    board_sound_alarm_set(sound_alarm_active);
    hv_enabled = board_hv_is_enabled();

    next_sample = board_millis;
    next_adc_sample = board_millis;
    next_minute_tick = board_millis + 1000u;
    screen_timeout_ms = board_display_screen_timeout_ms();
    inactivity_deadline = board_millis + screen_timeout_ms;

    for (;;) {
        board_display_service();
        board_click_sound_service();
        uint32_t now = board_millis;

        if (startup_led_active &&
            (int32_t)(now - startup_led_deadline) >= 0) {
            board_rgb_set(0u);
            startup_led_active = false;
        }

        if ((uint32_t)(now - next_sample) >= 10u) {
            uint8_t events = board_buttons_sample_10ms();
            uint32_t detected_pulses = board_detector_count();
            const uint8_t button_events = BOARD_BUTTON_LEFT_DOWN |
                BOARD_BUTTON_RIGHT_DOWN | BOARD_BUTTON_LEFT |
                BOARD_BUTTON_RIGHT | BOARD_BUTTON_LEFT_LONG |
                BOARD_BUTTON_RIGHT_LONG;

            if ((screen_alarm_active || light_alarm_active ||
                 sound_alarm_active) &&
                (events & (BOARD_BUTTON_LEFT_DOWN |
                           BOARD_BUTTON_RIGHT_DOWN)) != 0u) {
                alarm_suppressed_buttons |= events &
                    (BOARD_BUTTON_LEFT_DOWN | BOARD_BUTTON_RIGHT_DOWN);
                sound_alarm_active = false;
                alarms_acknowledged = true;
                board_sound_alarm_set(false);
            }

            if ((alarm_suppressed_buttons & BOARD_BUTTON_LEFT_DOWN) != 0u) {
                events &= (uint8_t)~(BOARD_BUTTON_LEFT_DOWN | BOARD_BUTTON_LEFT |
                                    BOARD_BUTTON_LEFT_LONG);
                if ((events & BOARD_BUTTON_LEFT_UP) != 0u) {
                    alarm_suppressed_buttons &=
                        (uint8_t)~BOARD_BUTTON_LEFT_DOWN;
                }
            }
            if ((alarm_suppressed_buttons & BOARD_BUTTON_RIGHT_DOWN) != 0u) {
                events &= (uint8_t)~(BOARD_BUTTON_RIGHT_DOWN |
                    BOARD_BUTTON_RIGHT | BOARD_BUTTON_RIGHT_LONG);
                if ((events & BOARD_BUTTON_RIGHT_UP) != 0u) {
                    alarm_suppressed_buttons &=
                        (uint8_t)~BOARD_BUTTON_RIGHT_DOWN;
                }
            }

            next_sample += 10u;
            if (!backlight_on &&
                (events & (BOARD_BUTTON_LEFT_DOWN |
                           BOARD_BUTTON_RIGHT_DOWN)) != 0u) {
                board_lcd_backlight_set(true);
                backlight_on = true;
                inactivity_deadline = now + screen_timeout_ms;
                wake_suppressed_buttons |= events &
                    (BOARD_BUTTON_LEFT_DOWN | BOARD_BUTTON_RIGHT_DOWN);
                events &= (uint8_t)~button_events;
            } else if ((events & button_events) != 0u) {
                inactivity_deadline = now + screen_timeout_ms;
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
                if (board_display_settings_list_active()) {
                    board_display_settings_list_exit();
                } else if (!board_display_rate_long_press()) {
                    board_display_extra_exit();
                }
                left_long_handled = true;
            }
            if ((events & BOARD_BUTTON_RIGHT_LONG) != 0u &&
                !right_long_handled) {
                if (board_display_settings_list_active()) {
                    board_display_settings_change();
                } else {
                    board_display_settings_list_activate();
                    board_display_extra_enter();
                }
                right_long_handled = true;
            }
            if ((events & BOARD_BUTTON_RIGHT) != 0u) {
                if (board_display_settings_list_active()) {
                    board_display_settings_move(true);
                } else {
                    board_display_right_short();
                }
            }
            if ((events & BOARD_BUTTON_LEFT) != 0u) {
                if (board_display_settings_list_active()) {
                    board_display_settings_move(false);
                } else {
                    board_display_left_short();
                }
            }

            {
                const bool new_hv_enabled = board_hv_is_enabled();
                if (new_hv_enabled != hv_enabled) {
                    hv_enabled = new_hv_enabled;
                    detected_pulses = board_detector_count();
                    event_count = detected_pulses;
                    previous_minute_sample = detected_pulses;
                    minute_total = 0u;
                    rate_window_seconds = 0u;
                    for (uint8_t i = 0u; i < 60u; ++i) {
                        minute_bins[i] = 0u;
                    }
                    if (hv_enabled) {
                        pulses_since_start = 0u;
                        board_display_set_count(0u);
                        board_display_set_rate(0u, 0u, 0u, false);
                    }
                    next_minute_tick = now + 1000u;
                }
            }

            {
                const bool alarm_active =
                    board_display_screen_alarm_active() &&
                    !alarms_acknowledged;
                if (alarm_active && !screen_alarm_active && !backlight_on) {
                    board_lcd_backlight_set(true);
                    backlight_on = true;
                    inactivity_deadline = now + screen_timeout_ms;
                } else if (!alarm_active && screen_alarm_active) {
                    inactivity_deadline = now + screen_timeout_ms;
                }
                screen_alarm_active = alarm_active;
            }

            {
                const bool alarm_active =
                    board_display_light_alarm_active() &&
                    !alarms_acknowledged;
                if (alarm_active != light_alarm_active) {
                    light_alarm_active = alarm_active;
                    light_alarm_on = alarm_active;
                    if (alarm_active) light_alarm_deadline = now + 100u;
                    board_flashlight_alarm_set(alarm_active, light_alarm_on);
                } else if (alarm_active &&
                           (int32_t)(now - light_alarm_deadline) >= 0) {
                    light_alarm_on = !light_alarm_on;
                    light_alarm_deadline = now +
                        (light_alarm_on ? 100u : 400u);
                    board_flashlight_alarm_set(true, light_alarm_on);
                }
            }

            {
                const bool previous_alarm_active = sound_alarm_active;
                const bool threat_active =
                    board_display_sound_alarm_active();
                if (alarms_acknowledged) {
                    sound_alarm_active = false;
                } else if (!threat_active) {
                    if (board_display_sound_alarm_stop_on_clear()) {
                        sound_alarm_active = false;
                    }
                } else {
                    sound_alarm_active = true;
                }
                if (sound_alarm_active != previous_alarm_active) {
                    board_sound_alarm_set(sound_alarm_active);
                }
            }

            if (!board_display_screen_alarm_active() &&
                !board_display_light_alarm_active() &&
                !board_display_sound_alarm_active()) {
                alarms_acknowledged = false;
            }

            if (detected_pulses != event_count) {
                event_count = detected_pulses;
                board_display_set_count(detected_pulses);
                if (!sound_alarm_active &&
                    board_display_click_sound_enabled(backlight_on)) {
                    board_click_sound_start();
                }
                if (!startup_led_active &&
                    board_display_led_flash_enabled(backlight_on)) {
                    board_display_set_rate_led();
                    event_led_on = true;
                    led_blink_deadline = now + 100u;
                }
            }

            {
                const uint32_t new_timeout_ms =
                    board_display_screen_timeout_ms();
                if (new_timeout_ms != screen_timeout_ms) {
                    screen_timeout_ms = new_timeout_ms;
                    inactivity_deadline = now + screen_timeout_ms;
                }
            }

            if (board_display_search_mode() && !backlight_on) {
                board_lcd_backlight_set(true);
                backlight_on = true;
                inactivity_deadline = now + screen_timeout_ms;
            }

            while ((int32_t)(now - next_minute_tick) >= 0) {
                if (session_elapsed_seconds < UINT32_MAX) {
                    ++session_elapsed_seconds;
                }
                if (hv_enabled) {
                const uint32_t second_pulses =
                    detected_pulses - previous_minute_sample;

                previous_minute_sample = detected_pulses;
                if (UINT32_MAX - pulses_since_start < second_pulses) {
                    pulses_since_start = UINT32_MAX;
                } else {
                    pulses_since_start += second_pulses;
                }
                if (UINT32_MAX - session_pulses_total < second_pulses) {
                    session_pulses_total = UINT32_MAX;
                } else {
                    session_pulses_total += second_pulses;
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
                if (rate_thousandths > session_max_rate_thousandths) {
                    session_max_rate_thousandths = rate_thousandths;
                }
                if (cpm > session_max_cpm) session_max_cpm = cpm;
                board_display_set_rate(rate_thousandths, cpm,
                                       pulses_since_start,
                                       pulses_since_start >= 25u);
                if (dose_measurement_seconds < UINT32_MAX) {
                    ++dose_measurement_seconds;
                }
                dose_fraction += rate_thousandths;
                dose_thousandths += dose_fraction / 3600u;
                dose_fraction %= 3600u;
                board_display_set_dose(dose_thousandths, dose_fraction,
                                       dose_measurement_seconds);
                }
                if (session_elapsed_seconds != 0u) {
                    const uint64_t integrated_rate =
                        (uint64_t)dose_thousandths * 3600u + dose_fraction;
                    const uint64_t average_rate_wide = integrated_rate /
                        session_elapsed_seconds;
                    const uint64_t average_cpm_wide =
                        ((uint64_t)session_pulses_total * 60u +
                            session_elapsed_seconds / 2u) /
                        session_elapsed_seconds;
                    const uint32_t average_rate = average_rate_wide >
                        UINT32_MAX ? UINT32_MAX : (uint32_t)average_rate_wide;
                    const uint32_t average_cpm = average_cpm_wide >
                        UINT32_MAX ? UINT32_MAX : (uint32_t)average_cpm_wide;
                    board_display_set_session_rates(average_rate,
                        session_max_rate_thousandths, average_cpm,
                        session_max_cpm, session_pulses_total);
                }
                next_minute_tick += 1000u;
            }

            if (backlight_on && !screen_alarm_active &&
                !board_display_search_mode() &&
                (int32_t)(now - inactivity_deadline) >= 0) {
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
