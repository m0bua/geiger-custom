#include "board.h"
#include "cpm_history.h"
#include "daily_dose.h"
#include "log_writer.h"
#include "protocol_v1.h"
#include "second_log.h"

#include <stdbool.h>
#include <stdint.h>

#define SOLAR_SAMPLE_INTERVAL_MS 300u
#define BATTERY_ACTIVE_SAMPLE_INTERVAL_MS 30000u
#define BATTERY_IDLE_SAMPLE_INTERVAL_MS 1800000u
#define BATTERY_CRITICAL_SAMPLE_INTERVAL_MS 30000u
#define USB_SAMPLE_INTERVAL_MS 60000u
#define TOTAL_PULSE_CHECKPOINT_MS 60000u

/* Stable image marker used by the browser flasher to recognize project builds. */
static const volatile uint8_t firmware_identity_marker[] = {
    0x4du, 0x47u, 0x32u, 0x46u, 0x57u, 0x49u, 0x44u, 0x31u
};

/* Dose checkpoints are shared with DAY in EEPROM bytes 316..347. */
static bool total_pulse_load(uint32_t *total, uint8_t *sequence)
{
    *total = daily_dose_total();
    *sequence = 0u;
    return true;
}

static bool total_pulse_save(uint32_t total, uint8_t *sequence)
{
    (void)sequence;
    daily_dose_set_total(total);
    return daily_dose_checkpoint();
}

static uint32_t selected_dose_pulses(uint8_t period, uint32_t uptime_pulses,
                                     uint32_t day_pulses,
                                     uint32_t total_pulses)
{
    if (period == 0u) return uptime_pulses;
    if (period == 2u) return total_pulses;
    return day_pulses;
}

static void dose_from_pulse_count(uint32_t pulse_count,
                                  uint32_t *dose_thousandths,
                                  uint32_t *dose_fraction)
{
    /* 0.00666 uSv/h per CPM means 0.000111 uSv per pulse.
     * Store dose in nSv plus a 1/3600 nSv remainder: one pulse is
     * 0.111 nSv ~= 400/3600 nSv. */
    const uint64_t scaled = (uint64_t)pulse_count * 400u;
    const uint64_t whole = scaled / 3600u;
    if (whole > UINT32_MAX) {
        *dose_thousandths = UINT32_MAX;
        *dose_fraction = 0u;
    } else {
        *dose_thousandths = (uint32_t)whole;
        *dose_fraction = (uint32_t)(scaled % 3600u);
    }
}

static void screen_backlight_wake(void)
{
    /* Keep the last PWM level; the rolling solar filter will ramp to target. */
    board_lcd_backlight_set(true);
}

int main(void)
{
    uint32_t next_sample;
    uint32_t next_solar_sample;
    uint32_t next_battery_sample;
    uint32_t next_usb_sample;
    uint32_t inactivity_deadline;
    uint32_t screen_timeout_ms;
    uint32_t event_count = 0u;
    uint32_t previous_minute_sample = 0u;
    uint32_t pulses_since_start = 0u;
    uint32_t session_pulses_total = 0u;
    uint32_t session_elapsed_seconds = 0u;
    uint32_t dose_thousandths = 0u;
    uint32_t dose_fraction = 0u;
    uint32_t dose_measurement_seconds = 0u;
    uint32_t today_pulse_count = 0u;
    uint32_t total_pulse_count = 0u;
    uint32_t total_pulse_saved = 0u;
    uint32_t next_total_checkpoint;
    uint8_t total_pulse_sequence = 0u;
    uint16_t calendar_day_key = UINT16_MAX;
    bool calendar_day_valid = false;
    uint32_t next_minute_tick;
    uint32_t next_rate_display_tick;
    uint32_t latest_cpm = 23u;
    uint32_t latest_display_cpm = 23u;
    uint32_t display_rate_cpm_tenths = 230u;
    uint16_t battery_adc_raw = 0u;
    uint16_t solar_adc_raw = 0u;
    uint16_t usb_adc_raw = 0u;
    uint32_t led_blink_deadline = 0u;
    uint32_t debug_usage_last_uptime;
    uint32_t debug_screen_seconds = 0u;
    uint32_t debug_sleep_seconds = 0u;
    uint32_t debug_usb_seconds = 0u;
    uint32_t light_alarm_deadline = 0u;
    uint32_t startup_led_deadline;
    uint32_t left_button_pressed_at = 0u;
    bool backlight_on = false;
    bool screen_alarm_active = false;
    bool light_alarm_active = false;
    bool sound_alarm_active = false;
    bool alarms_acknowledged = false;
    bool light_alarm_on = false;
    bool event_led_on = false;
    bool startup_led_active = true;
    bool idle_tick_active = false;
    bool idle_clock_active = false;
    bool adc_screen_was_on = false;
    bool debug_usage_screen_was_on;
    bool debug_usage_sleep_was_on;
    bool debug_usage_usb_was_on;
    bool hv_enabled;
    bool left_long_repeat_navigation = false;
    bool right_long_repeat_navigation = false;
    bool left_long_seen = false;
    bool right_long_seen = false;
    bool left_long_repeat_pending = false;
    bool right_long_repeat_pending = false;
    bool left_force_home_done = false;

    (void)firmware_identity_marker[0];
    board_init();
    board_adc_init();
    board_rgb_set(0u);
    board_rgb_set_brightness(30u);
    board_rgb_set(7u);
    startup_led_deadline = board_millis + 1000u;
    board_lcd_backlight_set_brightness(10u);
    board_lcd_backlight_set(false);
    board_display_init();
    (void)log_writer_init();
    second_log_init();
    daily_dose_init(UINT16_MAX, 0u);
    (void)total_pulse_load(&total_pulse_count, &total_pulse_sequence);
    {
        uint32_t uptime_seconds;
        uint32_t calendar_seconds;
        bool calendar_valid;
        if (board_rtc_log_time(&uptime_seconds, &calendar_seconds,
                &calendar_valid)) {
            if (calendar_valid) {
                calendar_day_key = (uint16_t)(calendar_seconds / 86400u);
                calendar_day_valid = true;
                daily_dose_set_day(calendar_day_key, 0u);
                today_pulse_count = daily_dose_count();
                dose_measurement_seconds = board_display_dose_period() == 1u ?
                    calendar_seconds % 86400u : 0u;
                dose_from_pulse_count(selected_dose_pulses(
                        board_display_dose_period(), 0u, today_pulse_count,
                        total_pulse_count),
                    &dose_thousandths, &dose_fraction);
            }
        }
    }
    if (board_display_dose_period() == 0u) {
        dose_measurement_seconds = board_uptime_seconds_get();
    }
    total_pulse_saved = total_pulse_count;
    dose_from_pulse_count(selected_dose_pulses(board_display_dose_period(),
            session_pulses_total, today_pulse_count, total_pulse_count),
        &dose_thousandths, &dose_fraction);
    board_display_set_dose(dose_thousandths, dose_fraction,
                           session_pulses_total, today_pulse_count,
                           total_pulse_count,
                           dose_measurement_seconds);
    screen_alarm_active = board_display_screen_alarm_active();
    sound_alarm_active = board_display_sound_alarm_active();
    board_sound_alarm_set(sound_alarm_active);
    hv_enabled = board_hv_is_enabled();
    if (!hv_enabled) {
        latest_cpm = 0u;
        latest_display_cpm = 0u;
        display_rate_cpm_tenths = 0u;
    }
    /* Ignore startup transients accumulated while RTC/display were coming up. */
    event_count = board_detector_count();
    previous_minute_sample = event_count;

    next_sample = board_millis;
    next_solar_sample = board_millis;
    next_battery_sample = board_millis;
    next_usb_sample = board_millis;
    next_minute_tick = board_millis + 1000u;
    next_rate_display_tick = board_millis + 500u;
    next_total_checkpoint = board_millis + TOTAL_PULSE_CHECKPOINT_MS;
    cpm_history_init();
    screen_timeout_ms = board_display_screen_timeout_ms();
    inactivity_deadline = board_millis + screen_timeout_ms;
    debug_usage_last_uptime = board_uptime_seconds_get();
    debug_usage_screen_was_on = backlight_on;
    debug_usage_sleep_was_on = idle_clock_active;
    debug_usage_usb_was_on = board_display_usb_connected();

    for (;;) {
        protocol_v1_service();
        uint32_t now = board_millis;
        if ((int32_t)(now - next_total_checkpoint) >= 0) {
            if (total_pulse_count != total_pulse_saved &&
                total_pulse_save(total_pulse_count, &total_pulse_sequence)) {
                total_pulse_saved = total_pulse_count;
            }
            (void)daily_dose_checkpoint();
            next_total_checkpoint = now + TOTAL_PULSE_CHECKPOINT_MS;
        }
        const uint8_t button_wakeup_mask =
            board_take_button_wakeup_request();
        const uint8_t currently_pressed = backlight_on ? 0u :
            board_buttons_pressed_mask();
        const uint8_t wake_buttons = button_wakeup_mask | currently_pressed;
        const bool exit_invisible_on_wake =
            board_display_invisible_mode() &&
            (wake_buttons & BOARD_BUTTON_LEFT_DOWN) != 0u;

        /* Button EXTI wakes immediately on the press edge; restore clocks and
         * light the display without waiting for the 10 ms debounce samples. */
        if (!backlight_on &&
            (button_wakeup_mask != 0u || currently_pressed != 0u)) {
            if (idle_clock_active) {
                (void)board_set_idle_clock(false);
                idle_clock_active = false;
            }
            if (idle_tick_active) {
                board_set_idle_tick(false);
                idle_tick_active = false;
            }
            now = board_millis;
            screen_backlight_wake();
            backlight_on = true;
            inactivity_deadline = now + screen_timeout_ms;
            if (exit_invisible_on_wake) board_display_left_short();
            board_buttons_suppress_until_release(wake_buttons);
        }

        if (startup_led_active &&
            (int32_t)(now - startup_led_deadline) >= 0) {
            board_rgb_set(0u);
            startup_led_active = false;
        }

        if ((uint32_t)(now - next_sample) >= 10u) {
            now = board_millis;
            uint8_t events = board_buttons_sample_10ms();
            uint32_t detected_pulses = board_detector_count();
            const uint8_t button_events = BOARD_BUTTON_LEFT_DOWN |
                BOARD_BUTTON_RIGHT_DOWN | BOARD_BUTTON_LEFT |
                BOARD_BUTTON_RIGHT | BOARD_BUTTON_LEFT_LONG |
                BOARD_BUTTON_RIGHT_LONG;

            /* A late loop samples once at the current time; it must not
             * replay old samples and manufacture a long press. */
            next_sample = now;
            if ((screen_alarm_active || light_alarm_active ||
                 sound_alarm_active) &&
                (events & (BOARD_BUTTON_LEFT_DOWN |
                           BOARD_BUTTON_RIGHT_DOWN)) != 0u) {
                board_buttons_suppress_until_release(events &
                    (BOARD_BUTTON_LEFT_DOWN | BOARD_BUTTON_RIGHT_DOWN));
                sound_alarm_active = false;
                alarms_acknowledged = true;
                board_sound_alarm_set(false);
                events &= (uint8_t)~button_events;
            }

            if (!backlight_on &&
                (events & (BOARD_BUTTON_LEFT_DOWN |
                           BOARD_BUTTON_RIGHT_DOWN)) != 0u) {
                const bool exit_invisible =
                    board_display_invisible_mode() &&
                    (events & BOARD_BUTTON_LEFT_DOWN) != 0u;
                if (idle_clock_active) {
                    (void)board_set_idle_clock(false);
                    idle_clock_active = false;
                }
                board_buttons_suppress_until_release(events &
                    (BOARD_BUTTON_LEFT_DOWN | BOARD_BUTTON_RIGHT_DOWN));
                screen_backlight_wake();
                backlight_on = true;
                inactivity_deadline = now + screen_timeout_ms;
                if (exit_invisible) board_display_left_short();
                events &= (uint8_t)~button_events;
            } else if ((events & button_events) != 0u) {
                inactivity_deadline = now + screen_timeout_ms;
            }
            if ((events & BOARD_BUTTON_LEFT_DOWN) != 0u) {
                left_button_pressed_at = now;
                left_force_home_done = false;
                left_long_repeat_navigation = false;
                left_long_seen = false;
                left_long_repeat_pending = false;
            }
            if ((events & BOARD_BUTTON_RIGHT_DOWN) != 0u) {
                right_long_repeat_navigation = false;
                right_long_seen = false;
                right_long_repeat_pending = false;
            }
            if ((events & BOARD_BUTTON_LEFT_LONG) != 0u) {
                if (!left_force_home_done &&
                    (uint32_t)(now - left_button_pressed_at) >= 3000u) {
                    left_force_home_done = true;
                    left_long_seen = true;
                    left_long_repeat_navigation = false;
                    left_long_repeat_pending = false;
                    board_display_force_home();
                    if (!backlight_on) {
                        screen_backlight_wake();
                        backlight_on = true;
                    }
                } else if (!left_force_home_done && !left_long_seen) {
                    left_long_seen = true;
                    if (board_display_settings_list_active()) {
                        board_display_settings_list_exit();
                    } else if (board_display_graph_back()) {
                        /* First back leaves the selected bar and restores
                         * period selection; another back exits to Dose. */
                    } else if (!board_display_rate_long_press()) {
                        board_display_extra_exit();
                    }
                    left_long_repeat_navigation =
                        board_display_settings_list_active() ||
                        board_display_extra_active();
                } else if (!left_force_home_done && left_long_repeat_navigation) {
                    /* Wait until the intermediate screen is fully drawn. */
                    left_long_repeat_pending = true;
                }
            }
            if ((events & BOARD_BUTTON_RIGHT_LONG) != 0u) {
                if (!right_long_seen) {
                    right_long_seen = true;
                    if (board_display_settings_list_active()) {
                        board_display_settings_change();
                    } else if (board_display_extra_active()) {
                        board_display_settings_list_activate();
                    } else if (board_display_graph_enter()) {
                        right_long_repeat_navigation = false;
                    } else if (board_display_graph_long_press()) {
                        right_long_repeat_navigation = false;
                    } else {
                        board_display_extra_enter();
                        right_long_repeat_navigation =
                            board_display_extra_active();
                    }
                } else if (right_long_repeat_navigation) {
                    /* Wait until the intermediate screen is fully drawn. */
                    right_long_repeat_pending = true;
                }
            }
            if ((events & BOARD_BUTTON_LEFT_UP) != 0u) {
                left_long_repeat_navigation = false;
                left_long_repeat_pending = false;
                left_long_seen = false;
                left_force_home_done = false;
            }
            if ((events & BOARD_BUTTON_RIGHT_UP) != 0u) {
                right_long_repeat_navigation = false;
                right_long_repeat_pending = false;
                right_long_seen = false;
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
                    second_log_reset_rate_window();
                    if (hv_enabled) {
                        pulses_since_start = 0u;
                        latest_cpm = 23u;
                        latest_display_cpm = 23u;
                        display_rate_cpm_tenths = 230u;
                        board_display_set_count(0u);
                        board_display_set_rate_alarm(0u, 0u, false);
                        board_display_set_rate(0u, 0u, 0u, false);
                    } else {
                        latest_cpm = 0u;
                        latest_display_cpm = 0u;
                        display_rate_cpm_tenths = 0u;
                        board_display_set_rate_alarm(0u, 0u, false);
                    }
                    next_minute_tick = now + 1000u;
                }
            }

            {
                const bool alarm_active =
                    board_display_screen_alarm_active() &&
                    !alarms_acknowledged;
                if (alarm_active && !screen_alarm_active && !backlight_on) {
                    if (idle_clock_active) {
                        (void)board_set_idle_clock(false);
                        idle_clock_active = false;
                    }
                    screen_backlight_wake();
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
                if (idle_clock_active) {
                    (void)board_set_idle_clock(false);
                    idle_clock_active = false;
                }
                screen_backlight_wake();
                backlight_on = true;
                inactivity_deadline = now + screen_timeout_ms;
            }

            while ((int32_t)(now - next_minute_tick) >= 0) {
                uint32_t log_uptime_seconds = board_uptime_seconds_get();
                uint32_t log_calendar_seconds = 0u;
                bool log_calendar_valid = false;
                (void)board_rtc_log_time(&log_uptime_seconds,
                    &log_calendar_seconds, &log_calendar_valid);
                {
                    if (log_calendar_valid) {
                        const uint16_t current_day = (uint16_t)(
                            log_calendar_seconds / 86400u);
                        if (!calendar_day_valid ||
                            current_day != calendar_day_key) {
                            const bool time_was_valid = calendar_day_valid;
                            calendar_day_key = current_day;
                            calendar_day_valid = true;
                            daily_dose_set_day(current_day,
                                time_was_valid ? 0u : session_pulses_total);
                            today_pulse_count = daily_dose_count();
                            if (board_display_dose_period() == 1u) {
                                dose_measurement_seconds =
                                    log_calendar_seconds % 86400u;
                            }
                            dose_from_pulse_count(selected_dose_pulses(
                                    board_display_dose_period(),
                                    session_pulses_total, today_pulse_count,
                                    total_pulse_count),
                                &dose_thousandths, &dose_fraction);
                            board_display_set_dose(dose_thousandths,
                                dose_fraction, session_pulses_total,
                                today_pulse_count, total_pulse_count,
                                dose_measurement_seconds);
                        }
                    }
                }
                if (hv_enabled) {
                if (session_elapsed_seconds < UINT32_MAX) {
                    ++session_elapsed_seconds;
                }
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
                if (UINT32_MAX - total_pulse_count < second_pulses) {
                    total_pulse_count = UINT32_MAX;
                } else {
                    total_pulse_count += second_pulses;
                }
                const uint32_t cpm = second_log_add(second_pulses);
                latest_cpm = cpm;
                latest_display_cpm = second_log_rate_cpm();
                daily_dose_add(second_pulses);
                today_pulse_count = calendar_day_valid ?
                    daily_dose_count() : session_pulses_total;
                log_writer_second(second_pulses, cpm,
                    log_uptime_seconds, log_calendar_seconds,
                    log_calendar_valid);
                if (board_display_dose_period() == 1u) {
                    if (dose_measurement_seconds < UINT32_MAX) {
                        ++dose_measurement_seconds;
                    }
                } else if (board_display_dose_period() == 0u) {
                    dose_measurement_seconds = board_uptime_seconds_get();
                } else {
                    dose_measurement_seconds = session_elapsed_seconds;
                }
                dose_from_pulse_count(selected_dose_pulses(
                        board_display_dose_period(), session_pulses_total,
                        today_pulse_count, total_pulse_count),
                    &dose_thousandths, &dose_fraction);
                board_display_set_dose(dose_thousandths, dose_fraction,
                                       session_pulses_total,
                                       today_pulse_count,
                                       total_pulse_count,
                                       dose_measurement_seconds);
                } else {
                    (void)second_log_add(0u);
                    latest_cpm = 0u;
                    latest_display_cpm = 0u;
                }
                const uint32_t measured_rate_thousandths = (uint32_t)(
                    ((uint64_t)latest_cpm * 666u + 50u) / 100u);
                board_display_set_rate_alarm(measured_rate_thousandths,
                    latest_cpm, pulses_since_start >= 30u);
                next_minute_tick += 1000u;
            }

            if ((int32_t)(now - next_rate_display_tick) >= 0) {
                const uint32_t target_cpm_tenths = latest_display_cpm * 10u;
                if (!backlight_on) {
                    display_rate_cpm_tenths = target_cpm_tenths;
                } else {
                    const int32_t difference = (int32_t)target_cpm_tenths -
                        (int32_t)display_rate_cpm_tenths;
                    int32_t step = difference / 2;
                    if (difference != 0 && step == 0) {
                        step = difference > 0 ? 1 : -1;
                    }
                    display_rate_cpm_tenths = (uint32_t)(
                        (int32_t)display_rate_cpm_tenths + step);
                }
                const uint32_t display_cpm =
                    (display_rate_cpm_tenths + 5u) / 10u;
                const uint32_t display_rate_thousandths = (uint32_t)(
                    ((uint64_t)display_rate_cpm_tenths * 666u + 500u) /
                    1000u);
                board_display_set_rate(display_rate_thousandths,
                    display_cpm, pulses_since_start,
                    pulses_since_start >= 30u);
                cpm_history_push(display_cpm);
                board_display_history_updated();
                next_rate_display_tick = now + 500u;
            }

            if (backlight_on && !screen_alarm_active &&
                !board_display_search_mode() &&
                !board_display_cleanup_feedback_active() &&
                (int32_t)(now - inactivity_deadline) >= 0) {
                backlight_on = false;
                board_lcd_backlight_set(false);
            }

            if (event_led_on &&
                (int32_t)(now - led_blink_deadline) >= 0) {
                board_rgb_set(0u);
                event_led_on = false;
            }
        }

        if (idle_clock_active && board_display_busy()) {
            (void)board_set_idle_clock(false);
            idle_clock_active = false;
        }
        board_display_service();
        if (left_long_repeat_pending && !board_display_busy()) {
            left_long_repeat_pending = false;
            if (board_display_settings_list_active()) {
                board_display_settings_list_exit();
            } else if (board_display_extra_active()) {
                board_display_extra_exit();
            }
            left_long_repeat_navigation =
                board_display_settings_list_active() ||
                board_display_extra_active();
        }
        if (right_long_repeat_pending && !board_display_busy()) {
            right_long_repeat_pending = false;
            if (board_display_extra_active() &&
                !board_display_settings_list_active()) {
                board_display_settings_list_activate();
            }
            /* Never repeat a value/edit action while held. */
            right_long_repeat_navigation = false;
        }
        if (board_display_take_cleanup_feedback_dismissed()) {
            inactivity_deadline = board_millis + screen_timeout_ms;
        }
        board_click_sound_service();
        now = board_millis;

        if (backlight_on != adc_screen_was_on) {
            adc_screen_was_on = backlight_on;
            if (backlight_on) {
                /* Refresh solar brightness and battery/USB as soon as the
                 * display wakes; then use their normal active intervals. */
                next_solar_sample = now;
                next_battery_sample = now;
            } else {
                const uint32_t idle_battery_interval =
                    board_display_battery_below_3_percent() ?
                        BATTERY_CRITICAL_SAMPLE_INTERVAL_MS :
                        BATTERY_IDLE_SAMPLE_INTERVAL_MS;
                next_battery_sample = now + idle_battery_interval;
            }
        }

        {
            const bool read_solar = backlight_on &&
                (int32_t)(now - next_solar_sample) >= 0;
            const bool battery_due =
                (int32_t)(now - next_battery_sample) >= 0;
            const bool read_usb = battery_due ||
                (int32_t)(now - next_usb_sample) >= 0;
            if (read_solar || battery_due || read_usb) {
                uint16_t channel2;
                uint16_t channel8;
                uint16_t channel9;

                board_adc_read_selected(battery_due, read_usb, read_solar,
                    &channel2, &channel8, &channel9);
                if (channel2 != UINT16_MAX) battery_adc_raw = channel2;
                if (channel8 != UINT16_MAX) usb_adc_raw = channel8;
                if (channel9 != UINT16_MAX) solar_adc_raw = channel9;
                board_display_set_adc(channel2, channel8, channel9);
                const uint32_t sampled_at = board_millis;
                if (read_solar) {
                    next_solar_sample = sampled_at +
                        SOLAR_SAMPLE_INTERVAL_MS;
                }
                if (battery_due) {
                    const uint32_t battery_interval = backlight_on ?
                        BATTERY_ACTIVE_SAMPLE_INTERVAL_MS :
                        (board_display_battery_below_3_percent() ?
                            BATTERY_CRITICAL_SAMPLE_INTERVAL_MS :
                            BATTERY_IDLE_SAMPLE_INTERVAL_MS);
                    next_battery_sample = sampled_at + battery_interval;
                }
                if (read_usb) {
                    next_usb_sample = sampled_at + USB_SAMPLE_INTERVAL_MS;
                }
            }
        }

        /* Use a 10 ms tick only after the LCD work queue has drained. This
         * cuts idle wakeups tenfold while preserving 1 ms service when active. */
        {
            const bool should_use_idle_tick =
                !backlight_on && !board_display_busy();
            if (should_use_idle_tick != idle_tick_active) {
                board_set_idle_tick(should_use_idle_tick);
                idle_tick_active = should_use_idle_tick;
            }
        }

        {
            const bool high_speed_needed = backlight_on ||
                board_display_busy() || startup_led_active ||
                board_display_usb_connected();
            if (high_speed_needed && idle_clock_active) {
                (void)board_set_idle_clock(false);
                idle_clock_active = false;
            } else if (!high_speed_needed && !idle_clock_active) {
                idle_clock_active = board_set_idle_clock(true);
            }
        }

        {
            /* Sleep time tracks the low-clock idle mode. Charge is independent
             * and may overlap with either screen or sleep time. */
            const uint32_t sample_uptime = board_uptime_seconds_get();
            const uint32_t elapsed_seconds = sample_uptime -
                debug_usage_last_uptime;
            if (debug_usage_screen_was_on) {
                debug_screen_seconds += elapsed_seconds;
            }
            if (debug_usage_sleep_was_on) {
                debug_sleep_seconds += elapsed_seconds;
            }
            if (debug_usage_usb_was_on) {
                debug_usb_seconds += elapsed_seconds;
            }
            debug_usage_last_uptime = sample_uptime;
            debug_usage_screen_was_on = backlight_on;
            debug_usage_sleep_was_on = idle_clock_active;
            debug_usage_usb_was_on = board_display_usb_connected();
            board_display_set_debug_usage(debug_screen_seconds,
                debug_sleep_seconds, debug_usb_seconds);
        }

        {
            protocol_v1_status_t status;
            status.uptime_seconds = board_uptime_seconds_get();
            status.count_today = today_pulse_count;
            status.rate_cpm = latest_display_cpm;
            status.dose_nsv = dose_thousandths;
            status.battery_raw = battery_adc_raw;
            status.solar_raw = solar_adc_raw;
            status.usb_raw = usb_adc_raw;
            status.flags = (uint16_t)((hv_enabled ? 1u : 0u) |
                (board_display_usb_connected() ? 2u : 0u) |
                ((board_display_battery_start_raw() & 0x0FFFu) << 4u));
            status.count_total = board_detector_count();
            status.dose_fraction_3600 = (uint16_t)dose_fraction;
            status.dose_count_total = total_pulse_count;
            status.screen_seconds = debug_screen_seconds;
            status.sleep_seconds = debug_sleep_seconds;
            status.usb_seconds = debug_usb_seconds;
            protocol_v1_set_status(&status);
        }
        protocol_v1_service();

        /* Keep clocks and interrupt-driven pulse counting alive while the CPU
         * waits for the next SysTick, detector, RTC, DMA, or button interrupt. */
        __asm volatile ("dsb\n\twfi" ::: "memory");
    }
}
