#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define BOARD_BUTTON_LEFT  (1u << 0)
#define BOARD_BUTTON_RIGHT (1u << 1)
#define BOARD_BUTTON_LEFT_LONG  (1u << 2)
#define BOARD_BUTTON_RIGHT_LONG (1u << 3)
#define BOARD_BUTTON_LEFT_DOWN  (1u << 4)
#define BOARD_BUTTON_RIGHT_DOWN (1u << 5)
#define BOARD_BUTTON_LEFT_UP    (1u << 6)
#define BOARD_BUTTON_RIGHT_UP   (1u << 7)

extern volatile uint32_t board_millis;

void board_init(void);
bool board_eeprom_read(uint16_t offset, uint8_t *value);
bool board_eeprom_write(uint16_t offset, uint8_t value);
bool board_rtc_log_time(uint32_t *uptime_seconds, uint32_t *calendar_seconds,
                        bool *calendar_valid);
uint32_t board_uptime_seconds_get(void);
void board_set_idle_tick(bool idle);
bool board_set_idle_clock(bool idle);
uint8_t board_buttons_sample_10ms(void);
void board_buttons_suppress_until_release(uint8_t mask);
uint8_t board_buttons_pressed_mask(void);
uint8_t board_take_button_wakeup_request(void);
uint32_t board_detector_count(void);
void board_hv_set_enabled(bool enabled);
bool board_hv_is_enabled(void);
void board_adc_init(void);
uint16_t board_adc_read(uint8_t channel);
void board_adc_read_selected(bool read_battery, bool read_usb, bool read_solar,
                             uint16_t *channel2, uint16_t *channel8,
                             uint16_t *channel9);
void board_rgb_set(uint8_t logical_rgb_mask);
void board_rgb_set_color(uint8_t red, uint8_t green, uint8_t blue);
void board_rgb_set_brightness(uint8_t percent);
void board_display_init(void);
void board_display_service(void);
void board_display_left_short(void);
void board_display_right_short(void);
bool board_display_rate_long_press(void);
bool board_display_graph_enter(void);
bool board_display_graph_long_press(void);
bool board_display_graph_back(void);
bool board_display_light_alarm_active(void);
bool board_display_dose_alarm_active(void);
bool board_display_sound_alarm_active(void);
bool board_display_sound_alarm_stop_on_clear(void);
void board_flashlight_alarm_set(bool active, bool on);
void board_display_extra_enter(void);
bool board_display_extra_active(void);
void board_display_extra_step(bool forward);
void board_display_extra_exit(void);
bool board_display_settings_list_active(void);
uint8_t board_display_settings_remote_count(void);
bool board_display_settings_remote_read(uint8_t *values, uint8_t capacity);
bool board_display_setting_remote_write(uint8_t setting_id, uint8_t value);
void board_display_settings_list_activate(void);
void board_display_settings_move(bool forward);
void board_display_settings_change(void);
void board_display_settings_list_exit(void);
void board_reboot(void);
void board_power_off(void);
void board_display_set_rate(uint32_t rate_thousandths, uint32_t cpm,
                            uint32_t pulses_since_start, bool window_ready);
void board_display_history_updated(void);
uint8_t board_display_rate_led_mask(void);
void board_display_set_rate_led(void);
void board_display_set_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction,
                            uint32_t daily_pulse_count,
                            uint32_t measurement_seconds);
void board_display_set_session_rates(uint32_t average_rate_thousandths,
                                     uint32_t maximum_rate_thousandths,
                                     uint32_t average_cpm,
                                     uint32_t maximum_cpm,
                                     uint32_t session_count_total);
void board_display_set_count(uint32_t count);
uint32_t board_display_screen_timeout_ms(void);
bool board_display_cleanup_feedback_active(void);
bool board_display_take_cleanup_feedback_dismissed(void);
bool board_display_led_flash_enabled(bool screen_on);
bool board_display_click_sound_enabled(bool screen_on);
bool board_display_search_mode(void);
bool board_display_use_spi(void);
bool board_display_usb_connected(void);
void board_click_sound_start(void);
void board_click_sound_service(void);
void board_sound_alarm_set(bool active);
bool board_display_screen_alarm_active(void);
void board_display_set_adc(uint16_t channel2, uint16_t channel8,
                           uint16_t channel9);
bool board_display_battery_below_3_percent(void);
bool board_display_busy(void);
void board_lcd_backlight_set(bool enabled);
void board_lcd_backlight_set_brightness(uint8_t percent);

#endif
