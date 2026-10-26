#include "board.h"
#include "cpm_history.h"
#include "log_storage.h"
#include "protocol_v1.h"

#include <stdbool.h>
#include <stdint.h>

#define SOLAR_SAMPLE_INTERVAL_MS 300u
#define BATTERY_ACTIVE_SAMPLE_INTERVAL_MS 30000u
#define BATTERY_IDLE_SAMPLE_INTERVAL_MS 600000u
#define BATTERY_CRITICAL_SAMPLE_INTERVAL_MS 30000u
#define USB_SAMPLE_INTERVAL_MS 60000u
#define TOTAL_PULSE_CHECKPOINT_MS 60000u
#define TOTAL_PULSE_MAGIC 0xC7u
#define TOTAL_PULSE_SLOT_COUNT 2u

/* Stable image marker used by the browser flasher to recognize project builds. */
static const volatile uint8_t firmware_identity_marker[] = {
    0x4du, 0x47u, 0x32u, 0x46u, 0x57u, 0x49u, 0x44u, 0x31u
};

/* TOTAL dose uses the unused EEPROM gaps 10-15/30-31/62-63/72-79.
 * Settings occupy 0-9, 16-29, 32-61, 64-71, 80-139; log storage starts at 140. */
static const uint16_t total_pulse_addresses[TOTAL_PULSE_SLOT_COUNT][9] = {
    {10u, 11u, 12u, 13u, 14u, 15u, 30u, 31u, 62u},
    {63u, 72u, 73u, 74u, 75u, 76u, 77u, 78u, 79u}
};

static uint16_t total_pulse_crc(const uint8_t *bytes, uint8_t count)
{
    uint16_t crc = 0xFFFFu;
    for (uint8_t i = 0u; i < count; ++i) {
        crc ^= (uint16_t)bytes[i] << 8u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static bool total_pulse_load(uint32_t *total, uint8_t *sequence)
{
    bool found = false;
    uint32_t newest_total = 0u;
    uint8_t newest_sequence = 0u;
    for (uint8_t slot = 0u; slot < TOTAL_PULSE_SLOT_COUNT; ++slot) {
        uint8_t record[9];
        for (uint8_t i = 0u; i < 9u; ++i) {
            if (!board_eeprom_read(total_pulse_addresses[slot][i], &record[i])) {
                return false;
            }
        }
        const uint32_t candidate = (uint32_t)record[1] |
            ((uint32_t)record[2] << 8u) | ((uint32_t)record[3] << 16u) |
            ((uint32_t)record[4] << 24u);
        const uint16_t crc = total_pulse_crc(record, 5u);
        if (record[7] != TOTAL_PULSE_MAGIC ||
            record[8] != (uint8_t)~TOTAL_PULSE_MAGIC ||
            record[5] != (uint8_t)crc || record[6] != (uint8_t)(crc >> 8u)) {
            continue;
        }
        if (!found || (int8_t)(record[0] - newest_sequence) > 0) {
            found = true;
            newest_total = candidate;
            newest_sequence = record[0];
        }
    }
    if (found) {
        *total = newest_total;
        *sequence = newest_sequence;
    }
    return found;
}

static bool total_pulse_save(uint32_t total, uint8_t *sequence)
{
    const uint8_t next_sequence = (uint8_t)(*sequence + 1u);
    const uint8_t slot = (uint8_t)(next_sequence % TOTAL_PULSE_SLOT_COUNT);
    uint8_t record[9] = {next_sequence, (uint8_t)total,
        (uint8_t)(total >> 8u), (uint8_t)(total >> 16u),
        (uint8_t)(total >> 24u), 0u, 0u, TOTAL_PULSE_MAGIC,
        (uint8_t)~TOTAL_PULSE_MAGIC};
    const uint16_t crc = total_pulse_crc(record, 5u);
    record[5] = (uint8_t)crc;
    record[6] = (uint8_t)(crc >> 8u);
    if (!board_eeprom_write(total_pulse_addresses[slot][7], 0u) ||
        !board_eeprom_write(total_pulse_addresses[slot][8], 0u)) return false;
    for (uint8_t i = 0u; i < 7u; ++i) {
        if (!board_eeprom_write(total_pulse_addresses[slot][i], record[i])) {
            return false;
        }
    }
    if (!board_eeprom_write(total_pulse_addresses[slot][7], record[7]) ||
        !board_eeprom_write(total_pulse_addresses[slot][8], record[8])) {
        return false;
    }
    *sequence = next_sequence;
    return true;
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
    uint32_t minute_total = 0u;
    uint32_t pulses_since_start = 0u;
    uint32_t session_pulses_total = 0u;
    uint32_t session_elapsed_seconds = 0u;
    uint32_t hour_min_cpm = UINT32_MAX;
    uint32_t hour_max_cpm = 0u;
    uint64_t hour_sum_cpm = 0u;
    uint32_t hour_sample_count = 0u;
    uint32_t hour_bucket = UINT32_MAX;
    uint8_t rate_window_seconds = 0u;
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
    uint32_t minute_bins[60] = {0u};
    uint32_t next_minute_tick;
    uint32_t next_cpm_history_tick;
    uint32_t latest_cpm = 0u;
    uint16_t battery_adc_raw = 0u;
    uint16_t solar_adc_raw = 0u;
    uint16_t usb_adc_raw = 0u;
    uint8_t minute_head = 0u;
    uint32_t led_blink_deadline = 0u;
    uint32_t high_speed_deadline = 0u;
    uint32_t debug_usage_last_uptime;
    uint32_t debug_screen_seconds = 0u;
    uint32_t debug_sleep_seconds = 0u;
    uint32_t debug_usb_seconds = 0u;
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
    bool idle_tick_active = false;
    bool idle_clock_active = false;
    bool adc_screen_was_on = false;
    bool debug_usage_screen_was_on;
    bool debug_usage_sleep_was_on;
    bool debug_usage_usb_was_on;
    bool log_checkpoint_on_wake = false;
    bool hv_enabled;
    bool left_long_repeat_navigation = false;
    bool right_long_repeat_navigation = false;
    bool left_long_seen = false;
    bool right_long_seen = false;
    bool left_long_repeat_pending = false;
    bool right_long_repeat_pending = false;

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
    log_storage_init(board_display_use_spi());
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
                const bool day_checkpoint_matches =
                    log_storage_calendar_day(calendar_day_key, true);
                log_storage_restore_live(calendar_seconds, true);
                today_pulse_count = log_storage_today_pulse_count();
                if ((!day_checkpoint_matches || today_pulse_count == 0u) &&
                    log_storage_spi_available()) {
                    today_pulse_count = log_storage_today_recover_from_hours(
                        (uint8_t)((calendar_seconds % 86400u) / 3600u));
                }
                dose_measurement_seconds = board_display_dose_period() == 1u ?
                    calendar_seconds % 86400u : 0u;
                dose_from_pulse_count(selected_dose_pulses(
                        board_display_dose_period(), 0u, today_pulse_count,
                        total_pulse_count),
                    &dose_thousandths, &dose_fraction);
            }
            log_storage_time_cutoff(uptime_seconds, calendar_seconds,
                calendar_valid, LOG_CUTOFF_REASON_BOOT);
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
    /* Ignore startup transients accumulated while RTC/display were coming up. */
    event_count = board_detector_count();
    previous_minute_sample = event_count;

    next_sample = board_millis;
    next_solar_sample = board_millis;
    next_battery_sample = board_millis;
    next_usb_sample = board_millis;
    next_minute_tick = board_millis + 1000u;
    next_total_checkpoint = board_millis + TOTAL_PULSE_CHECKPOINT_MS;
    cpm_history_init();
    next_cpm_history_tick = board_millis + 500u;
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
            next_total_checkpoint = now + TOTAL_PULSE_CHECKPOINT_MS;
        }
        const uint8_t button_wakeup_mask =
            board_take_button_wakeup_request();
        const uint8_t currently_pressed = backlight_on ? 0u :
            board_buttons_pressed_mask();

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
            log_checkpoint_on_wake = true;
            inactivity_deadline = now + screen_timeout_ms;
            board_buttons_suppress_until_release(button_wakeup_mask |
                currently_pressed);
        }

        if (startup_led_active &&
            (int32_t)(now - startup_led_deadline) >= 0) {
            board_rgb_set(0u);
            startup_led_active = false;
        }

        if ((uint32_t)(now - next_sample) >= 10u) {
            /* Run the periodic 64-bit rate/dose calculations at full speed.
             * MSI remains the clock between these brief service intervals. */
            if (idle_clock_active &&
                (int32_t)(now - next_minute_tick) >= 0) {
                (void)board_set_idle_clock(false);
                idle_clock_active = false;
            }
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
                if (idle_clock_active) {
                    (void)board_set_idle_clock(false);
                    idle_clock_active = false;
                }
                board_buttons_suppress_until_release(events &
                    (BOARD_BUTTON_LEFT_DOWN | BOARD_BUTTON_RIGHT_DOWN));
                screen_backlight_wake();
                backlight_on = true;
                log_checkpoint_on_wake = true;
                inactivity_deadline = now + screen_timeout_ms;
                events &= (uint8_t)~button_events;
            } else if ((events & button_events) != 0u) {
                inactivity_deadline = now + screen_timeout_ms;
            }
            if ((events & BOARD_BUTTON_LEFT_DOWN) != 0u) {
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
                if (!left_long_seen) {
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
                } else if (left_long_repeat_navigation) {
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
                    if (idle_clock_active) {
                        (void)board_set_idle_clock(false);
                        idle_clock_active = false;
                    }
                    screen_backlight_wake();
                    backlight_on = true;
                    log_checkpoint_on_wake = true;
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
                    if (sound_alarm_active && idle_clock_active) {
                        (void)board_set_idle_clock(false);
                        idle_clock_active = false;
                    }
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
                high_speed_deadline = now + 500u;
                if (idle_clock_active) {
                    (void)board_set_idle_clock(false);
                    idle_clock_active = false;
                }
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
                log_checkpoint_on_wake = true;
                inactivity_deadline = now + screen_timeout_ms;
            }

            while ((int32_t)(now - next_minute_tick) >= 0) {
                {
                    uint32_t uptime_seconds;
                    uint32_t calendar_seconds;
                    bool calendar_valid;
                    if (board_rtc_log_time(&uptime_seconds,
                            &calendar_seconds, &calendar_valid) &&
                        calendar_valid) {
                        const uint16_t current_day = (uint16_t)(
                            calendar_seconds / 86400u);
                        if (!calendar_day_valid ||
                            current_day != calendar_day_key) {
                            const bool time_was_valid = calendar_day_valid;
                            const bool day_matches =
                                log_storage_calendar_day(current_day,
                                                         time_was_valid);
                            calendar_day_key = current_day;
                            calendar_day_valid = true;
                            if (!time_was_valid && !day_matches) {
                                log_storage_today_seed(session_pulses_total);
                                today_pulse_count = session_pulses_total;
                            } else {
                                today_pulse_count =
                                    log_storage_today_pulse_count();
                            }
                            if (board_display_dose_period() == 1u) {
                                dose_measurement_seconds =
                                    calendar_seconds % 86400u;
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
                        log_storage_checkpoint(calendar_seconds, true,
                            board_display_usb_connected(),
                            log_checkpoint_on_wake);
                        log_checkpoint_on_wake = false;
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
                latest_cpm = cpm;
                log_storage_second(second_pulses);
                today_pulse_count = calendar_day_valid ?
                    log_storage_today_pulse_count() : session_pulses_total;
                log_storage_alert_sample(cpm, second_pulses,
                    board_uptime_seconds_get(),
                    pulses_since_start >= 25u);
                board_display_set_rate(rate_thousandths, cpm,
                                       pulses_since_start,
                                       pulses_since_start >= 25u);
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
                    log_storage_alert_sample(0u, 0u,
                        board_uptime_seconds_get(), false);
                }
                {
                    uint32_t minute_min = UINT32_MAX;
                    uint32_t minute_max = 0u;
                    uint64_t minute_sum = 0u;
                    uint32_t minute_samples = cpm_history_count();
                    if (minute_samples > 120u) minute_samples = 120u;
                    for (uint32_t age = 0u; age < minute_samples; ++age) {
                        uint32_t sample;
                        if (cpm_history_get((uint8_t)age, &sample)) {
                            if (sample < minute_min) minute_min = sample;
                            if (sample > minute_max) minute_max = sample;
                            minute_sum += sample;
                        }
                    }
                    if (minute_samples == 0u) minute_min = 0u;
                    const uint32_t minute_average = minute_samples == 0u ? 0u :
                        (uint32_t)(minute_sum / minute_samples);
                    const uint32_t current_hour_bucket =
                        board_uptime_seconds_get() / 3600u;
                    if (current_hour_bucket != hour_bucket) {
                        hour_bucket = current_hour_bucket;
                        hour_min_cpm = UINT32_MAX;
                        hour_max_cpm = 0u;
                        hour_sum_cpm = 0u;
                        hour_sample_count = 0u;
                    }
                    if (latest_cpm < hour_min_cpm) hour_min_cpm = latest_cpm;
                    if (latest_cpm > hour_max_cpm) hour_max_cpm = latest_cpm;
                    hour_sum_cpm += latest_cpm;
                    if (hour_sample_count < UINT32_MAX) ++hour_sample_count;
                    board_display_set_rate_stats(minute_min, minute_average,
                        minute_max, hour_min_cpm == UINT32_MAX ? 0u : hour_min_cpm,
                        hour_sample_count == 0u ? 0u :
                            (uint32_t)(hour_sum_cpm / hour_sample_count),
                        hour_max_cpm);
                }
                next_minute_tick += 1000u;
            }

            if ((int32_t)(now - next_cpm_history_tick) >= 0) {
                cpm_history_push(latest_cpm);
                if (hv_enabled) log_storage_sample_cpm(latest_cpm);
                board_display_history_updated();
                /* Do not fabricate a burst of old samples after a delayed
                 * service pass; resume the regular 500 ms cadence from now. */
                next_cpm_history_tick = now + 500u;
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

                if (idle_clock_active) {
                    (void)board_set_idle_clock(false);
                    idle_clock_active = false;
                }
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
                board_display_busy() || startup_led_active || event_led_on ||
                light_alarm_active || sound_alarm_active ||
                board_display_usb_connected() ||
                (int32_t)(now - high_speed_deadline) < 0;
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
            status.rate_cpm = latest_cpm;
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
