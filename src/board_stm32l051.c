#include "board.h"
#include "board_config.h"
#include "button_input.h"
#include "cpm_history.h"
#include "font_large.h"
#include "font_small.h"
#include "protocol_v1.h"
#include "second_log.h"
#include "log_history.h"
#include "log_writer.h"
#include "log_eeprom.h"
#include "spi_log.h"
#include "spi_nor.h"
#include "lcd_theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

volatile uint32_t board_millis;
static volatile uint32_t board_uptime_seconds;
static volatile uint16_t board_uptime_millisecond_remainder;
static volatile uint8_t systick_period_ms = 1u;
static bool idle_clock_active;
static volatile uint32_t detector_pulse_count;
static volatile bool board_hv_enabled = true;
static volatile uint16_t lcd_adc_battery_raw;
static uint16_t lcd_adc_battery_start_raw;
static volatile uint16_t lcd_adc_sun_raw;
static volatile uint16_t lcd_adc_usb_raw;
static bool adc_ready;
static bool adc_calibrated;
static volatile bool power_off_wakeup;
static volatile uint8_t button_wakeup_pending_mask;
/* Runtime-only indication mode: INVISIBLE, NORMAL, SEARCH. */
static uint8_t lcd_indication_mode = 1u;
static uint32_t lcd_search_mode_started;

#define DETECTOR_GPIO_BASE GPIOB_BASE
#define DETECTOR_PIN       BOARD_DETECTOR_PIN
#define DETECTOR_EXTI_MASK (1u << DETECTOR_PIN)
#define FLASHLIGHT_PIN     8u
#define LCD_BATTERY_PERCENT_STABLE_MS 90000u
#define LCD_SOLAR_BRIGHTNESS_SAMPLE_COUNT 34u
#define LCD_BACKLIGHT_RAMP_INTERVAL_MS 200u

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define RCC_BASE       0x40021000u
#define RCC_CR         REG32(RCC_BASE + 0x00u)
#define RCC_ICSCR      REG32(RCC_BASE + 0x04u)
#define RCC_CFGR       REG32(RCC_BASE + 0x0Cu)
#define RCC_AHBENR     REG32(RCC_BASE + 0x30u)
#define RCC_CCIPR      REG32(RCC_BASE + 0x4Cu)
#define RCC_IOPENR     REG32(RCC_BASE + 0x2Cu)
#define RCC_APB1ENR    REG32(RCC_BASE + 0x38u)
#define RCC_APB2ENR    REG32(RCC_BASE + 0x34u)
/* STM32L051: +0x24 is APB2RSTR (bit 0 holds SYSCFG in reset), NOT CSR. */
#define RCC_CSR        REG32(RCC_BASE + 0x50u)
#define PWR_CR         REG32(0x40007000u)
#define RTC_BASE       0x40002800u
#define RTC_TR         REG32(RTC_BASE + 0x00u)
#define RTC_DR         REG32(RTC_BASE + 0x04u)
#define RTC_CR         REG32(RTC_BASE + 0x08u)
#define RTC_ISR        REG32(RTC_BASE + 0x0Cu)
#define RTC_PRER       REG32(RTC_BASE + 0x10u)
#define RTC_WPR        REG32(RTC_BASE + 0x24u)
#define RTC_BKP0R      REG32(RTC_BASE + 0x50u)
#define RTC_BKP1R      REG32(RTC_BASE + 0x54u)
#define ADC_BASE       0x40012400u
#define ADC_ISR        REG32(ADC_BASE + 0x00u)
#define ADC_CR         REG32(ADC_BASE + 0x08u)
#define ADC_CFGR1      REG32(ADC_BASE + 0x0Cu)
#define ADC_CFGR2      REG32(ADC_BASE + 0x10u)
#define ADC_SMPR       REG32(ADC_BASE + 0x14u)
#define ADC_CHSELR     REG32(ADC_BASE + 0x28u)
#define ADC_DR         REG32(ADC_BASE + 0x40u)
#define SPI2_BASE      0x40003800u
#define LPTIM1_BASE    0x40007C00u
#define LPTIM_ISR      REG32(LPTIM1_BASE + 0x00u)
#define LPTIM_ICR      REG32(LPTIM1_BASE + 0x04u)
#define LPTIM_CFGR     REG32(LPTIM1_BASE + 0x0Cu)
#define LPTIM_CR       REG32(LPTIM1_BASE + 0x10u)
#define LPTIM_CMP      REG32(LPTIM1_BASE + 0x14u)
#define LPTIM_ARR      REG32(LPTIM1_BASE + 0x18u)
#define DMA1_BASE      0x40020000u
#define DMA1_ISR       REG32(DMA1_BASE + 0x00u)
#define DMA1_IFCR      REG32(DMA1_BASE + 0x04u)
#define DMA1_CH5_CCR   REG32(DMA1_BASE + 0x58u)
#define DMA1_CH5_CNDTR REG32(DMA1_BASE + 0x5Cu)
#define DMA1_CH5_CPAR  REG32(DMA1_BASE + 0x60u)
#define DMA1_CH5_CMAR  REG32(DMA1_BASE + 0x64u)
#define DMA1_CSELR     REG32(DMA1_BASE + 0xA8u)
#define SPI_CR1        0x00u
#define SPI_CR2        0x04u
#define SPI_SR         0x08u
#define SPI_DR         0x0Cu
#define FLASH_ACR      REG32(0x40022000u)
#define FLASH_PECR     REG32(0x40022004u)
#define FLASH_PEKEYR   REG32(0x4002200Cu)
#define FLASH_SR       REG32(0x40022018u)
#define DATA_EEPROM_BASE 0x08080000u
#define FLASH_PECR_PELOCK (1u << 0)
#define FLASH_PECR_DATA   (1u << 4)
#define FLASH_SR_BSY      (1u << 0)

#define GPIOA_BASE     0x50000000u
#define GPIOB_BASE     0x50000400u
#define GPIO_MODER     0x00u
#define GPIO_OTYPER    0x04u
#define GPIO_OSPEEDR   0x08u
#define GPIO_PUPDR     0x0Cu
#define GPIO_IDR       0x10u
#define GPIO_BSRR      0x18u
#define GPIO_AFRL      0x20u
#define GPIO_AFRH      0x24u
#define SYSCFG_BASE    0x40010000u
#define EXTI_BASE      0x40010400u
#define EXTI_IMR       REG32(EXTI_BASE + 0x00u)
#define EXTI_RTSR      REG32(EXTI_BASE + 0x08u)
#define EXTI_FTSR      REG32(EXTI_BASE + 0x0Cu)
#define EXTI_PR        REG32(EXTI_BASE + 0x14u)
#define NVIC_ISER      REG32(0xE000E100u)
#define SCB_SCR        REG32(0xE000ED10u)
#define SCB_AIRCR      REG32(0xE000ED0Cu)
#define SCB_ICSR       REG32(0xE000ED04u)

#define TIM2_BASE      0x40000000u
#define TIM22_BASE     0x40011400u
#define TIM_CR1        0x00u
#define TIM_EGR        0x14u
#define TIM_CCMR1      0x18u
#define TIM_CCMR2      0x1Cu
#define TIM_CCER       0x20u
#define TIM_CNT        0x24u
#define TIM_PSC        0x28u
#define TIM_ARR        0x2Cu
#define TIM_CCR1       0x34u
#define TIM_CCR2       0x38u
#define TIM_CCR4       0x40u

#define PWM_PERIOD     1000u
#define BACKLIGHT_PWM_PERIOD 1000u
#define LCD_QUEUE_CAPACITY 192u
#define LCD_SPI_ENABLED 1u
#define LCD_DIAGNOSTIC_COMMANDS 1u
#define LCD_WIDTH 160u
#define LCD_HEIGHT 80u
#define LCD_PANEL_Y_OFFSET 24u
#define LCD_Y_OFFSET 28u
#define LCD_SMALL_TEXT_GAP 1u
#define LCD_EXTRA_SCREEN_COUNT 2u
#define LCD_GRAPH_PAGE 5u
#define LCD_DEBUG_SUBPAGE_COUNT 3u
#define LCD_GRAPH_BAR_GAP 1u
#define LCD_GRAPH_BAR_STEP (FONT_SMALL_CELL_WIDTH + LCD_GRAPH_BAR_GAP)
#define LCD_GRAPH_VISIBLE_BARS ((LCD_GRAPH_PLOT_WIDTH + LCD_GRAPH_BAR_GAP) / \
    LCD_GRAPH_BAR_STEP)
#define LCD_GRAPH_LOG_MAX_RECORDS 60u
#define LCD_GRAPH_LOG_BATCH_RECORDS 13u
#define LCD_GRAPH_CURSOR_CENTER (LCD_GRAPH_VISIBLE_BARS / 2u)
#define LCD_GRAPH_PENDING_LEFT  (-1)
#define LCD_GRAPH_PENDING_RIGHT (1)
#define LCD_GRAPH_PENDING_TOGGLE (3)
#define LCD_GRAPH_PARTIAL_CURSOR 1u
#define LCD_GRAPH_PARTIAL_WINDOW 2u
#define LCD_GRAPH_PARTIAL_LIVE 3u
#define LCD_GRAPH_TOP (FONT_SMALL_CELL_HEIGHT + \
    (FONT_SMALL_CELL_HEIGHT / 2u))
#define LCD_GRAPH_BASELINE (LCD_HEIGHT - 10u)
#define LCD_GRAPH_HEIGHT (LCD_GRAPH_BASELINE - LCD_GRAPH_TOP + 1u)
#define LCD_GRAPH_STATS_WIDTH ((7u * 7u) + 4u)
#define LCD_GRAPH_PLOT_WIDTH (LCD_WIDTH - LCD_GRAPH_STATS_WIDTH)
#define LCD_GRAPH_YELLOW_THRESHOLD_CPM 61u
#define LCD_GRAPH_RED_THRESHOLD_CPM 136u
#define LCD_GRAPH_CHUNK_HEIGHT FONT_SMALL_CELL_HEIGHT
#define LCD_GRAPH_CHUNK_COUNT ((LCD_GRAPH_HEIGHT + \
    LCD_GRAPH_CHUNK_HEIGHT - 1u) / LCD_GRAPH_CHUNK_HEIGHT)
#define LCD_RATE_MAX_VALUE 9999999u
#define LCD_RATE_MAX_DIGITS 5u
#define LCD_RATE_MAX_GLYPHS (LCD_RATE_MAX_DIGITS + 1u)
#define LCD_RATE_MASK_GLYPHS 6u
#define LCD_CPM_MAX_DIGITS 4u
#define LCD_RATE_FIELD_WIDTH (LCD_RATE_MASK_GLYPHS * FONT_LARGE_CELL_WIDTH)
#define LCD_RATE_FIELD_X ((LCD_WIDTH - LCD_RATE_FIELD_WIDTH) / 2u)
#define LCD_RATE_LABEL_LENGTH 5u
#define LCD_DOSE_LABEL_LENGTH 5u
#define LCD_CPM_TEXT_LENGTH 11u
#define LCD_DOSE_ROW_OFF 0xFFu
#define LCD_DOSE_ROW_TIME 0xFEu
#define LCD_DOSE_ROW_CPM 0xFDu
#define LCD_DOSE_ROW_PERIOD_BASE 3u
#define LCD_WARMUP_TEXT_LENGTH 6u
#define LCD_WARMUP_BOX_WIDTH 64u
#define LCD_WARMUP_BOX_HEIGHT (FONT_SMALL_CELL_HEIGHT + 2u)
#define LCD_ADC_TEXT_LENGTH 18u
#define LCD_CPM_TEXT_WIDTH (LCD_CPM_TEXT_LENGTH * FONT_SMALL_CELL_WIDTH + \
    (LCD_CPM_TEXT_LENGTH - 1u) * LCD_SMALL_TEXT_GAP)
#define LCD_LARGE_GLYPH_PIXELS (FONT_LARGE_CELL_WIDTH * FONT_LARGE_CELL_HEIGHT)
#define LCD_TOP_ROW_PIXELS (LCD_WIDTH * FONT_SMALL_CELL_HEIGHT)
#define LCD_TEXT_GLYPH_PIXELS ((LCD_LARGE_GLYPH_PIXELS > \
    LCD_TOP_ROW_PIXELS) ? LCD_LARGE_GLYPH_PIXELS : \
    LCD_TOP_ROW_PIXELS)
static void adc_wait_ms(uint32_t duration);
static bool adc_enable(void);
static void adc_disable(void);
static void lcd_apply_brightness_settings(void);
static void lcd_backlight_auto_ramp_service(void);
static void lcd_format_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction);
static void lcd_format_dose_uptime_row(void);
static uint8_t lcd_dose_row_option(uint8_t option_index);
static bool button_pressed(uint32_t base, uint32_t pin);
static void pin_input_with_pull(uint32_t base, uint32_t pin);
static void board_reset(void);

#define SYST_CSR       REG32(0xE000E010u)
#define SYST_RVR       REG32(0xE000E014u)
#define SYST_CVR       REG32(0xE000E018u)

static button_input_t left_button;
static button_input_t right_button;
static uint8_t rgb_brightness_percent = 100u;
static uint8_t backlight_brightness_percent = 100u;
static bool backlight_pwm_running;
static bool backlight_requested_on;
static uint8_t rgb_channels;
static uint8_t rgb_level[3] = {255u, 255u, 255u};
static bool lcd_spi_ready = true;
static bool lcd_spi_error;
static bool lcd_started;
static bool lcd_ready;
static uint8_t lcd_init_state;
static uint32_t lcd_init_deadline;
static bool lcd_background_pending;
static bool lcd_fill_start_pending;
static bool lcd_fill_active;
static bool lcd_dma_active;
static uint32_t lcd_dma_deadline;
static bool lcd_dma_is_text_row;
static uint8_t lcd_dma_text_glyph_advance;
static bool lcd_text_active;
static bool lcd_cpm_redraw_pending;
static bool lcd_dose_row_redraw_pending;
static bool lcd_main_rows_redraw_pending;
static bool lcd_text_main_rows_only;
static bool lcd_debug_redraw_pending;
static bool lcd_settings_redraw_pending;
static bool lcd_text_row_commands_queued;
static uint8_t lcd_text_glyph_index;
static uint8_t lcd_text_dma_glyph_advance;
static uint16_t lcd_text_glyph_pixels;
static char lcd_rate_glyphs[LCD_RATE_MAX_GLYPHS + 2u] = "      ";
static uint8_t lcd_rate_glyph_count = LCD_RATE_MAX_GLYPHS;
static bool lcd_rate_has_measurement;
static bool lcd_dose_has_measurement;
static uint16_t lcd_rate_color = LCD_COUNTER_BLUE;
static uint16_t lcd_alarm_rate_color = LCD_COUNTER_BLUE;
static uint8_t lcd_rate_led_mask = 5u;
static char lcd_rate_label[] = "mSv/h";
static char lcd_display_unit_prefix = 'm';
static char lcd_cpm_text[LCD_CPM_TEXT_LENGTH + 1u] = "   0 CPM   ";
static char lcd_adc_glyphs[LCD_ADC_TEXT_LENGTH + 1u] =
    "      \x01  0% \x02  0% ";
static uint16_t lcd_displayed_battery_percent;
static uint16_t lcd_battery_percent_candidate;
static uint32_t lcd_battery_percent_candidate_since_ms;
static bool lcd_battery_percent_initialized;
static uint16_t lcd_battery_start_percent;
static uint16_t lcd_displayed_solar_percent;
static bool lcd_solar_sample_ready;
static uint16_t lcd_solar_brightness_samples[
    LCD_SOLAR_BRIGHTNESS_SAMPLE_COUNT];
static uint8_t lcd_solar_brightness_sample_count;
static uint8_t lcd_solar_brightness_sample_next;
static uint8_t lcd_auto_backlight_target_percent = 1u;
static uint32_t lcd_backlight_ramp_last_ms;
static uint8_t lcd_screen_page;
static uint8_t lcd_main_return_page;
static uint8_t lcd_last_extra_page = 2u;
static uint8_t lcd_debug_subpage;
static uint32_t lcd_debug_screen_seconds;
static uint32_t lcd_debug_sleep_seconds;
static uint32_t lcd_debug_usb_seconds;
static uint8_t lcd_graph_range_index = 3u;
static uint8_t lcd_graph_log_type;
static uint8_t lcd_graph_selected;
static uint8_t lcd_graph_selected_record;
static uint8_t lcd_graph_log_window_start;
static uint32_t lcd_graph_selected_time;
static bool lcd_graph_selected_calendar;
static bool lcd_graph_cursor_mode;
static bool lcd_graph_log_window_valid;
static bool lcd_graph_log_summary_valid;
static bool lcd_graph_loading;
static bool lcd_graph_log_summary_work_active;
static uint8_t lcd_graph_log_visible;
static uint8_t lcd_graph_log_available;
static log_point_t lcd_graph_log_points[LCD_GRAPH_VISIBLE_BARS];
static log_cursor_t lcd_graph_log_record_cursors[LCD_GRAPH_LOG_MAX_RECORDS];
static log_cursor_t lcd_graph_log_summary_cursor;
static uint8_t lcd_graph_log_summary_count;
static uint8_t lcd_graph_column_y[LCD_WIDTH];
static char lcd_graph_stat_values[4][5];
static bool lcd_graph_input_locked;
static int8_t lcd_graph_pending_action;
static bool lcd_graph_partial_pending;
static uint8_t lcd_graph_partial_kind;
static uint8_t lcd_graph_previous_selected;
static uint32_t lcd_graph_stat_min;
static uint32_t lcd_graph_stat_max;
static uint64_t lcd_graph_log_summary_dose;
static uint32_t lcd_graph_log_summary_max;
static uint32_t lcd_graph_log_summary_average;
static uint32_t lcd_graph_log_summary_bar_max;
static uint32_t lcd_graph_log_summary_coverage;
static uint8_t lcd_graph_yellow_threshold_y = UINT8_MAX;
static uint8_t lcd_graph_red_threshold_y = UINT8_MAX;
static uint32_t lcd_graph_refresh_deadline;
static void lcd_graph_reset_log_window(void);
static bool lcd_graph_load_log_window(uint8_t start);
static bool lcd_graph_read_log_point(uint8_t index, log_point_t *point);
static bool lcd_graph_update_log_summary(void);
static bool lcd_graph_prepare_partial_glyph(void);
static __attribute__((noinline)) void lcd_render_small_line(
    const char line[20], uint16_t row_width, uint16_t base_color,
    uint8_t selected_start, uint8_t selected_end,
    uint8_t special_start, uint8_t special_end, uint16_t special_color);
static bool lcd_settings_list_active;
static uint8_t lcd_settings_selected;
static uint32_t lcd_settings_hidden_mask;
static bool settings_spi_enabled;
static uint8_t settings_log_memory;
static uint8_t settings_log_source;
static bool lcd_cleanup_working_pending;
static bool lcd_cleanup_feedback_dismissed;
static uint8_t lcd_cleanup_selected;
static uint8_t lcd_cleanup_status;
static uint32_t lcd_cleanup_status_deadline;
static bool lcd_settings_editing;
static uint8_t lcd_settings_edit_value;
static bool lcd_settings_spi_failed;
static bool rtc_ready;
static bool rtc_time_valid;
static uint8_t rtc_init_error;
typedef enum {
    SETTING_LED, SETTING_CLICKS, SETTING_RATE_SCREEN, SETTING_RATE_LIGHT,
    SETTING_RATE_SOUND, SETTING_LED_BRIGHT, SETTING_SCREEN_BRIGHT,
    SETTING_SCREEN_TIMER, SETTING_LEAD_ZERO, SETTING_UNIT, SETTING_RATE_PREFIX,
    SETTING_DOSE_PREFIX, SETTING_RATE_ROW_2, SETTING_USE_SPI, SETTING_RESET,
    SETTING_HV, SETTING_RATE_CUSTOM, SETTING_ALARM_STOP, SETTING_POWER_OFF,
    SETTING_REBOOT, SETTING_TONE, SETTING_LONG_PRESS, SETTING_DOSE_ROW_2,
    SETTING_DOSE_PERIOD, SETTING_DOSE_SCREEN, SETTING_DOSE_LIGHT,
    SETTING_DOSE_SOUND, SETTING_DOSE_CUSTOM, SETTING_LED_TIME,
    SETTING_COUNT
} setting_id_t;

static uint8_t lcd_settings_value_index[SETTING_COUNT] = {
    [SETTING_LED] = 2u, [SETTING_SCREEN_TIMER] = 2u,
    [SETTING_RATE_PREFIX] = 1u, [SETTING_DOSE_PREFIX] = 1u,
    [SETTING_LONG_PRESS] = 3u, [SETTING_RATE_ROW_2] = 0u,
    [SETTING_DOSE_ROW_2] = 1u, [SETTING_DOSE_PERIOD] = 1u,
    [SETTING_RATE_CUSTOM] = 4u
};
static bool click_sound_active;
static uint8_t click_sound_profile;
static uint32_t click_sound_deadline;
static bool sound_alarm_mode;
static uint8_t sound_alarm_phase;
static bool rgb_flash_active;

static uint32_t board_rgb_tim2_compare(void)
{
    const uint32_t period = click_sound_active ?
        REG32(TIM2_BASE + TIM_ARR) + 1u : PWM_PERIOD;
    return ((uint32_t)rgb_brightness_percent * period) / 100u;
}

static uint32_t board_rgb_channel_compare(uint8_t channel)
{
    return (board_rgb_tim2_compare() * rgb_level[channel]) / 255u;
}

static void board_rgb_apply(void)
{
    const uint32_t red_compare = (rgb_channels & 1u) ?
        board_rgb_channel_compare(0u) : 0u;
    const uint32_t green_compare = (rgb_channels & 2u) ?
        board_rgb_channel_compare(1u) : 0u;
    const uint32_t blue_compare = (rgb_channels & 4u) ?
        (((uint32_t)rgb_brightness_percent * PWM_PERIOD) / 100u *
         rgb_level[2]) / 255u : 0u;
    const bool tim2_active = click_sound_active || red_compare != 0u ||
        green_compare != 0u;

    /* Enable clocks only while touching/configuring the PWM peripherals. */
    RCC_APB1ENR |= 1u; /* TIM2 */
    RCC_APB2ENR |= 1u << 5u; /* TIM22 */
    (void)RCC_APB2ENR;
    REG32(TIM2_BASE + TIM_CCR1) = red_compare;
    REG32(TIM2_BASE + TIM_CCR2) = green_compare;
    REG32(TIM22_BASE + TIM_CCR1) = blue_compare;
    REG32(TIM2_BASE + TIM_EGR) = 1u;
    REG32(TIM22_BASE + TIM_EGR) = 1u;

    if (tim2_active) {
        REG32(TIM2_BASE + TIM_CR1) |= 1u;
    } else {
        REG32(TIM2_BASE + TIM_CR1) &= ~1u;
        RCC_APB1ENR &= ~1u;
    }
    if (blue_compare != 0u) {
        REG32(TIM22_BASE + TIM_CR1) |= 1u;
    } else {
        REG32(TIM22_BASE + TIM_CR1) &= ~1u;
        RCC_APB2ENR &= ~(1u << 5u);
    }
}
static bool flashlight_enabled;
static bool flashlight_manual_enabled;
static bool flashlight_alarm_override;
static bool flashlight_alarm_output;
static uint32_t lcd_displayed_minute_key = UINT32_MAX;
static uint32_t lcd_rate_value_thousandths;
static uint32_t lcd_alarm_rate_value_thousandths;
static uint32_t lcd_current_cpm;
static bool lcd_alarm_rate_window_ready;
static uint32_t lcd_warmup_pulses;
static bool lcd_rate_window_ready;
static bool lcd_warmup_box_visible;
static uint32_t lcd_count_total;
static uint32_t lcd_daily_pulse_count;
static uint32_t lcd_dose_period_pulses[3];
static uint8_t lcd_settings_sequence;
static uint8_t lcd_row_settings_sequence;
static uint8_t lcd_timer_sequence;
static uint8_t lcd_alarm_sequence;
static uint8_t lcd_light_alarm_sequence;
static uint8_t lcd_dose_alarm_sequence;
static uint8_t lcd_hv_sequence;
static uint32_t lcd_dose_value_thousandths;
static uint32_t lcd_dose_fraction;
static uint32_t lcd_dose_measurement_seconds;
static bool lcd_dose_row_hidden;
/* Keep lossless RGB565 colours in a small per-glyph palette. DMA expands
 * one display-width chunk at a time, rather than holding a full RGB565 row. */
static uint8_t lcd_text_glyph[LCD_TEXT_GLYPH_PIXELS] __attribute__((aligned(4)));
static uint16_t lcd_text_palette[32];
static uint8_t lcd_text_palette_count;
static uint16_t lcd_text_dma_pixels[LCD_WIDTH];
static uint16_t lcd_text_stream_offset;
static uint16_t lcd_text_stream_total;
static uint8_t lcd_text_stream_advance;

static void lcd_glyph_put(uint16_t index, uint16_t color)
{
    uint8_t entry = 0u;
    while (entry < lcd_text_palette_count && lcd_text_palette[entry] != color) ++entry;
    if (entry == lcd_text_palette_count && entry < 32u) lcd_text_palette[lcd_text_palette_count++] = color;
    if (index < LCD_TEXT_GLYPH_PIXELS) lcd_text_glyph[index] = entry < 32u ? entry : 0u;
}

static uint16_t lcd_draw_pixel;
typedef struct {
    uint8_t value;
    uint8_t is_data;
} lcd_queue_item_t;
static lcd_queue_item_t lcd_queue[LCD_QUEUE_CAPACITY];
static uint16_t lcd_queue_head;
static uint16_t lcd_queue_tail;

enum {
    LCD_INIT_IDLE = 0u,
    LCD_INIT_BOARD_ENABLE_DELAY,
    LCD_INIT_RESET_LOW,
    LCD_INIT_RESET_HIGH,
    LCD_INIT_SWRESET_TX,
    LCD_INIT_SWRESET_DELAY,
    LCD_INIT_SLPOUT_TX,
    LCD_INIT_SLPOUT_DELAY,
    LCD_INIT_CONFIG_TX,
    LCD_INIT_NORON_DELAY,
    LCD_INIT_NORON_TX,
    LCD_INIT_DISPON_DELAY,
    LCD_INIT_DISPON_TX,
    LCD_INIT_FINAL_DELAY,
    LCD_INIT_POST_INIT_INVOFF_TX,
    LCD_INIT_READY,
    LCD_INIT_FAILED
};

static bool deadline_reached(uint32_t deadline)
{
    return (int32_t)(board_millis - deadline) >= 0;
}

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_calendar_t;

static bool rtc_read_calendar(rtc_calendar_t *calendar);

#define RTC_CONFIG_MAGIC 0x47523255u /* v2: correct RTCEN/RTCRST setup */
#define RTC_TIME_VALID_MAGIC 0x54494D45u
#define RTC_ISR_KEEP_FLAGS_CLEAR_RSF 0x00017F5Fu

static uint8_t rtc_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10u) << 4u) | (value % 10u));
}

static void board_rtc_init(void);

static uint8_t rtc_from_bcd(uint32_t value)
{
    return (uint8_t)(((value >> 4u) & 0x0Fu) * 10u + (value & 0x0Fu));
}

static uint8_t rtc_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t month_days[12] = {
        31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u
    };
    if (month == 0u || month > 12u) return 31u;
    if (month == 2u && (year % 4u) == 0u) return 29u;
    return month_days[month - 1u];
}

static uint32_t rtc_calendar_seconds_since_2000(const rtc_calendar_t *calendar)
{
    uint32_t days = 0u;
    for (uint16_t year = 2000u; year < calendar->year; ++year) {
        days += (year % 4u) == 0u ? 366u : 365u;
    }
    for (uint8_t month = 1u; month < calendar->month; ++month) {
        days += rtc_days_in_month(calendar->year, month);
    }
    days += (uint32_t)calendar->day - 1u;
    return (((days * 24u + calendar->hour) * 60u + calendar->minute) *
        60u) + calendar->second;
}

static bool rtc_calendar_from_seconds_since_2000(uint32_t seconds,
                                                  rtc_calendar_t *calendar)
{
    uint32_t days = seconds / 86400u;
    uint32_t day_seconds = seconds % 86400u;
    uint16_t year = 2000u;

    while (days >= ((year % 4u) == 0u ? 366u : 365u)) {
        days -= (year % 4u) == 0u ? 366u : 365u;
        if (++year > 2099u) return false;
    }
    uint8_t month = 1u;
    while (month <= 12u && days >= rtc_days_in_month(year, month)) {
        days -= rtc_days_in_month(year, month);
        ++month;
    }
    if (month > 12u) return false;

    calendar->year = year;
    calendar->month = month;
    calendar->day = (uint8_t)(days + 1u);
    calendar->hour = (uint8_t)(day_seconds / 3600u);
    calendar->minute = (uint8_t)((day_seconds / 60u) % 60u);
    calendar->second = 0u;
    return true;
}

bool board_rtc_log_time(uint32_t *uptime_seconds, uint32_t *calendar_seconds,
                        bool *calendar_valid)
{
    rtc_calendar_t calendar;
    bool valid;
    if (uptime_seconds == NULL || calendar_seconds == NULL ||
        calendar_valid == NULL) return false;
    valid = rtc_time_valid && rtc_read_calendar(&calendar);
    *uptime_seconds = board_uptime_seconds;
    *calendar_valid = valid;
    *calendar_seconds = valid ? rtc_calendar_seconds_since_2000(&calendar) : 0u;
    return true;
}

uint8_t board_rtc_debug_status(void)
{
    if (rtc_init_error != 0u) return rtc_init_error;
    if (!rtc_ready) return 7u;
    return rtc_time_valid ? 5u : 6u;
}

uint8_t board_rtc_clock_source(void)
{
    return ((RCC_CSR >> 16u) & 3u) == 1u ? 1u : 0u;
}

uint32_t board_uptime_seconds_get(void)
{
    return board_uptime_seconds;
}

static bool rtc_wait_flag(volatile uint32_t *reg, uint32_t mask,
                          bool set)
{
    /* Bound oscillator startup wait so a missing crystal cannot stall boot. */
    uint32_t timeout = 2000000u;
    while (((*reg & mask) != 0u) != set && --timeout != 0u) { }
    return timeout != 0u;
}

static bool rtc_exit_init_and_sync(void)
{
    /* Clear INIT and RSF while writing 1s to unrelated write-0-clear flags. */
    RTC_ISR = RTC_ISR_KEEP_FLAGS_CLEAR_RSF & ~(1u << 7u);
    return rtc_wait_flag(&RTC_ISR, 1u << 5u, true);
}

static void rtc_seed_from_build(rtc_calendar_t *calendar)
{
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    const char *date = __DATE__;
    const char *time = __TIME__;

    calendar->year = (uint16_t)((date[7] - '0') * 1000u +
        (date[8] - '0') * 100u + (date[9] - '0') * 10u + date[10] - '0');
    calendar->month = 1u;
    for (uint8_t i = 0u; i < 12u; ++i) {
        if (date[0] == months[i][0] && date[1] == months[i][1] &&
            date[2] == months[i][2]) {
            calendar->month = (uint8_t)(i + 1u);
            break;
        }
    }
    calendar->day = (uint8_t)((date[4] == ' ' ? 0u :
        (uint8_t)(date[4] - '0') * 10u) + (uint8_t)(date[5] - '0'));
    calendar->hour = (uint8_t)((time[0] - '0') * 10u + time[1] - '0');
    calendar->minute = (uint8_t)((time[3] - '0') * 10u + time[4] - '0');
    calendar->second = (uint8_t)((time[6] - '0') * 10u + time[7] - '0');
}

static bool rtc_read_calendar(rtc_calendar_t *calendar)
{
    uint32_t time;
    uint32_t date;

    if (!rtc_ready) return false;
    /* TR locks the calendar shadow registers; DR must be the final read to
     * unlock them. A trailing TR would leave the date shadow locked. */
    time = RTC_TR;
    date = RTC_DR;
    calendar->hour = rtc_from_bcd((time >> 16u) & 0x3Fu);
    calendar->minute = rtc_from_bcd((time >> 8u) & 0x7Fu);
    calendar->second = rtc_from_bcd(time & 0x7Fu);
    calendar->year = (uint16_t)(2000u +
        rtc_from_bcd((date >> 16u) & 0xFFu));
    calendar->month = rtc_from_bcd((date >> 8u) & 0x1Fu);
    calendar->day = rtc_from_bcd(date & 0x3Fu);
    return calendar->month >= 1u && calendar->month <= 12u &&
        calendar->day >= 1u &&
        calendar->day <= rtc_days_in_month(calendar->year, calendar->month) &&
        calendar->hour < 24u && calendar->minute < 60u &&
        calendar->second < 60u;
}

static bool rtc_write_calendar(const rtc_calendar_t *calendar,
                               bool mark_time_valid)
{
    uint32_t timeout = 20000000u;
    if (!rtc_ready) {
        /* Retry a startup that may have raced RTC oscillator stabilization. */
        board_rtc_init();
        if (!rtc_ready) {
            return false;
        }
    }
    if (calendar->year < 2000u || calendar->year > 2099u ||
        calendar->month < 1u || calendar->month > 12u ||
        calendar->day < 1u ||
        calendar->day > rtc_days_in_month(calendar->year, calendar->month) ||
        calendar->hour > 23u || calendar->minute > 59u ||
        calendar->second > 59u) return false;

    RCC_APB1ENR |= 1u << 28u;
    PWR_CR |= 1u << 8u; /* Reassert backup-domain write access. */
    RTC_WPR = 0xCAu;
    RTC_WPR = 0x53u;
    RTC_ISR |= 1u << 7u; /* INIT */
    while ((RTC_ISR & (1u << 6u)) == 0u && --timeout != 0u) { }
    if (timeout == 0u) {
        RTC_WPR = 0xFFu;
        return false;
    }
    RTC_TR = ((uint32_t)rtc_to_bcd(calendar->hour) << 16u) |
        ((uint32_t)rtc_to_bcd(calendar->minute) << 8u) |
        rtc_to_bcd(calendar->second);
    RTC_DR = ((uint32_t)rtc_to_bcd((uint8_t)(calendar->year % 100u)) << 16u) |
        ((uint32_t)rtc_to_bcd(calendar->month) << 8u) |
        rtc_to_bcd(calendar->day) | (1u << 13u);
    if (!rtc_exit_init_and_sync()) {
        RTC_WPR = 0xFFu;
        return false;
    }
    RTC_WPR = 0xFFu;
    if (mark_time_valid) {
        RTC_BKP1R = RTC_TIME_VALID_MAGIC;
        rtc_time_valid = true;
    }
    return true;
}

bool board_rtc_set_time(uint32_t seconds_since_2000)
{
    rtc_calendar_t calendar;
    if (!rtc_calendar_from_seconds_since_2000(seconds_since_2000, &calendar)) {
        return false;
    }
    calendar.second = (uint8_t)(seconds_since_2000 % 60u);
    return rtc_write_calendar(&calendar, true);
}

static void board_rtc_init(void)
{
    rtc_calendar_t seed;
    uint32_t timeout;
    uint32_t source;

    rtc_ready = false;
    rtc_time_valid = false;
    rtc_init_error = 0u;
    RCC_APB1ENR |= 1u << 28u; /* PWR interface clock */
    (void)RCC_APB1ENR;       /* Let the peripheral clock enable propagate. */
    PWR_CR |= 1u << 8u;       /* allow backup-domain writes */
    if (!rtc_wait_flag(&PWR_CR, 1u << 8u, true)) {
        rtc_init_error = 4u;
        return;
    }
    source = (RCC_CSR >> 16u) & 3u;
    if (RTC_BKP0R == RTC_CONFIG_MAGIC &&
        (RCC_CSR & ((1u << 18u) | (1u << 19u))) == (1u << 18u) &&
        ((source == 1u && (RCC_CSR & (1u << 9u)) != 0u) ||
         (source == 2u && (RCC_CSR & (1u << 1u)) != 0u)) &&
        (RTC_ISR & ((1u << 4u) | (1u << 7u))) == (1u << 4u) &&
        (RTC_CR & ((1u << 6u) | (1u << 5u))) == 0u) {
        /* A backup marker alone does not prove that the RTC is running.
         * Resynchronize its shadow registers after a system reset. */
        RTC_WPR = 0xCAu;
        RTC_WPR = 0x53u;
        rtc_ready = rtc_exit_init_and_sync();
        RTC_WPR = 0xFFu;
        rtc_time_valid = rtc_ready && RTC_BKP1R == RTC_TIME_VALID_MAGIC;
        if (!rtc_ready) rtc_init_error = 3u;
        return;
    }

    /* Reset before starting LSE: backup reset also clears LSE configuration. */
    RCC_CSR |= 1u << 19u;
    RCC_CSR &= ~(1u << 19u);

    /* First setup only: prefer an accurate 32.768 kHz crystal, then use LSI. */
    RCC_CSR |= 1u << 8u; /* LSEON */
    if (rtc_wait_flag(&RCC_CSR, 1u << 9u, true)) {
        source = 1u;
    } else {
        RCC_CSR |= 1u; /* LSION */
        if (!rtc_wait_flag(&RCC_CSR, 1u << 1u, true)) {
            rtc_init_error = 1u;
            return;
        }
        RCC_CSR &= ~(1u << 8u); /* Do not leave the unused LSE running. */
        source = 2u;
    }

    RCC_CSR = (RCC_CSR & ~((3u << 16u) | (1u << 18u))) |
        (source << 16u) | (1u << 18u); /* RTCSEL + RTCEN */

    RTC_WPR = 0xCAu;
    RTC_WPR = 0x53u;
    RTC_ISR |= 1u << 7u;
    timeout = 1000000u;
    while ((RTC_ISR & (1u << 6u)) == 0u && --timeout != 0u) { }
    if (timeout == 0u) {
        rtc_init_error = 2u;
        RTC_WPR = 0xFFu;
        return;
    }
    RTC_CR &= ~((1u << 6u) | (1u << 5u)); /* 24-hour, shadow registers on. */
    /* LSE: 128 x 256; LSI nominal ~37 kHz: 128 x 289. */
    RTC_PRER = (127u << 16u) | (source == 1u ? 255u : 288u);
    rtc_seed_from_build(&seed);
    RTC_TR = ((uint32_t)rtc_to_bcd(seed.hour) << 16u) |
        ((uint32_t)rtc_to_bcd(seed.minute) << 8u) |
        rtc_to_bcd(seed.second);
    RTC_DR = ((uint32_t)rtc_to_bcd((uint8_t)(seed.year % 100u)) << 16u) |
        ((uint32_t)rtc_to_bcd(seed.month) << 8u) |
        rtc_to_bcd(seed.day) | (1u << 13u);
    if (!rtc_exit_init_and_sync()) {
        rtc_init_error = 3u;
        RTC_WPR = 0xFFu;
        return;
    }
    RTC_WPR = 0xFFu;
    RTC_BKP1R = 0u;
    RTC_BKP0R = RTC_CONFIG_MAGIC;
    rtc_ready = true;
    rtc_time_valid = false;
    rtc_init_error = 0u;
}

static const uint64_t lcd_sv_unit_scales[4] = {
    18000000000ull, 18000000u, 18000u, 18u
};

static void lcd_format_scaled_sv(uint64_t value_picosv_x18,
                                 uint8_t prefix_setting,
                                 bool dose_value)
{
    /* Scales are multiplied by 18 to retain the dose accumulator's
     * 1/18-pSv remainder without floating-point arithmetic. */
    static const char unit_prefixes[] = { 'm', (char)0xB5u, 'n', 'p' };
    uint8_t unit = 3u;
    uint8_t fallback_unit = 3u;
    const uint8_t selected_prefix = lcd_settings_value_index[prefix_setting];

    /* The stock firmware's R display is its conventional x100 scaling of Sv. */
    if (lcd_settings_value_index[SETTING_UNIT] == 2u) {
        value_picosv_x18 *= 100u;
    }
    bool has_fallback = false;

    if (selected_prefix < 3u) {
        unit = selected_prefix;
    } else {
        for (uint8_t i = 0u; i < 4u; ++i) {
            const uint64_t whole = value_picosv_x18 / lcd_sv_unit_scales[i];
            if (whole >= 10u && whole < 1000u) {
                unit = i;
                has_fallback = false;
                break;
            }
            if (whole != 0u && !has_fallback) {
                fallback_unit = i;
                has_fallback = true;
            }
        }
    }
    if (selected_prefix == 3u && has_fallback) {
        /* A value from 1 to 9.999 has no adjacent SI prefix that yields
         * 10..999, so retain that unit and pad to two integer places. */
        const uint64_t whole = value_picosv_x18 / lcd_sv_unit_scales[unit];
        if (whole < 10u || whole >= 1000u) {
            unit = fallback_unit;
        }
    }

    for (;;) {
        const uint64_t scale = lcd_sv_unit_scales[unit];
        const uint64_t whole = value_picosv_x18 / scale;
        uint8_t integer_digits = 1u;
        uint8_t decimals;
        uint8_t decimal_limit;
        uint64_t value = whole;
        uint64_t factor = 1u;
        uint64_t scaled;
        uint64_t integer_part;
        uint64_t fraction;
        uint64_t divisor;
        char reversed[5];

        while (value >= 10u) {
            value /= 10u;
            ++integer_digits;
        }
        decimal_limit = dose_value ? 5u :
            (unit == 0u ? 5u :
                (unit == 1u ?
                    (lcd_settings_value_index[SETTING_UNIT] == 2u ? 1u : 3u) :
                    (unit == 2u ? 0u : 5u)));
        if (dose_value && whole == 0u &&
            lcd_settings_value_index[SETTING_LEAD_ZERO] == 0u && decimal_limit > 4u) {
            /* Keep the visible leading zero plus decimal point within the
             * six-character large-font field. */
            decimal_limit = 4u;
        }
        decimals = whole == 0u ? 5u :
            (integer_digits < 5u ? (uint8_t)(5u - integer_digits) : 0u);
        if (decimals > decimal_limit) decimals = decimal_limit;
        for (uint8_t i = 0u; i < decimals; ++i) {
            factor *= 10u;
        }
        scaled = (value_picosv_x18 * factor + scale / 2u) / scale;
        integer_part = scaled / factor;

        /* If rounding moves the value into a higher integer order, reduce
         * decimal places to keep the same order-dependent precision. */
        for (;;) {
            uint8_t rounded_digits = 1u;
            uint8_t adjusted_decimals;
            value = integer_part;
            while (value >= 10u) {
                value /= 10u;
                ++rounded_digits;
            }
            adjusted_decimals = integer_part == 0u ? 5u :
                (rounded_digits < 5u ?
                    (uint8_t)(5u - rounded_digits) : 0u);
            if (adjusted_decimals > decimal_limit) {
                adjusted_decimals = decimal_limit;
            }
            if (adjusted_decimals >= decimals) break;
            decimals = adjusted_decimals;
            factor = 1u;
            for (uint8_t i = 0u; i < decimals; ++i) factor *= 10u;
            scaled = (value_picosv_x18 * factor + scale / 2u) / scale;
            integer_part = scaled / factor;
        }

        /* Rounding can cross 1000. Move to the next coarser unit so the
         * displayed value keeps at most three places before the separator. */
        if (integer_part >= 1000u && unit != 0u &&
            (selected_prefix == 3u || integer_part > 99999u)) {
            --unit;
            continue;
        }

        fraction = scaled % factor;
        value = integer_part;
        integer_digits = 0u;
        if (value == 0u) {
            reversed[integer_digits++] = '0';
        } else {
            do {
                reversed[integer_digits++] = (char)('0' + value % 10u);
                value /= 10u;
            } while (value != 0u && integer_digits < sizeof(reversed));
        }

        lcd_rate_glyph_count = 0u;
        if (integer_part != 0u || decimals == 0u ||
            lcd_settings_value_index[SETTING_LEAD_ZERO] == 0u) {
            while (integer_digits != 0u) {
                lcd_rate_glyphs[lcd_rate_glyph_count++] =
                    reversed[--integer_digits];
            }
        }
        if (decimals != 0u) {
            lcd_rate_glyphs[lcd_rate_glyph_count++] = '.';
            divisor = factor / 10u;
            for (uint8_t i = 0u; i < decimals; ++i) {
                lcd_rate_glyphs[lcd_rate_glyph_count++] =
                    (char)('0' + ((fraction / divisor) % 10u));
                if (divisor > 1u) {
                    divisor /= 10u;
                }
            }
        }
        if (lcd_rate_glyph_count == 0u) {
            lcd_rate_glyphs[lcd_rate_glyph_count++] = '0';
        }
        lcd_rate_glyphs[lcd_rate_glyph_count] = '\0';
        lcd_display_unit_prefix = unit_prefixes[unit];
        lcd_rate_label[0] = lcd_display_unit_prefix;
        if (lcd_settings_value_index[SETTING_UNIT] == 2u) {
            lcd_rate_label[1] = 'R';
            lcd_rate_label[2] = '/';
            lcd_rate_label[3] = 'h';
            lcd_rate_label[4] = ' ';
        } else {
            lcd_rate_label[1] = 'S';
            lcd_rate_label[2] = 'v';
            lcd_rate_label[3] = '/';
            lcd_rate_label[4] = 'h';
        }
        return;
    }
}

static void lcd_format_count_value(uint32_t value, const char *label)
{
    char reversed[6];
    uint8_t digits = 0u;

    if (value > 999999u) value = 999999u;
    do {
        reversed[digits++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && digits < sizeof(reversed));
    lcd_rate_glyph_count = 0u;
    while (digits != 0u) {
        lcd_rate_glyphs[lcd_rate_glyph_count++] = reversed[--digits];
    }
    lcd_rate_glyphs[lcd_rate_glyph_count] = '\0';
    for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) {
        lcd_rate_label[i] = label[i];
    }
}

static void lcd_format_count_rate(uint32_t cpm)
{
    if (lcd_settings_value_index[SETTING_RATE_PREFIX] == 0u) {
        lcd_format_count_value(cpm, "CPM  ");
    } else {
        const uint32_t thousandths_cps = (uint32_t)(
            ((uint64_t)cpm * 1000u + 30u) / 60u);
        const uint32_t whole = thousandths_cps / 1000u;
        const uint32_t fraction = thousandths_cps % 1000u;

        lcd_format_count_value(whole, "CPS  ");
        if (whole == 0u && lcd_settings_value_index[SETTING_LEAD_ZERO] != 0u) {
            lcd_rate_glyph_count = 0u;
        }
        lcd_rate_glyphs[lcd_rate_glyph_count++] = '.';
        lcd_rate_glyphs[lcd_rate_glyph_count++] =
            (char)('0' + (fraction / 100u));
        lcd_rate_glyphs[lcd_rate_glyph_count++] =
            (char)('0' + ((fraction / 10u) % 10u));
        lcd_rate_glyphs[lcd_rate_glyph_count++] =
            (char)('0' + (fraction % 10u));
        lcd_rate_glyphs[lcd_rate_glyph_count] = '\0';
    }
}

static void lcd_format_rate(uint32_t rate_thousandths)
{
    if (lcd_settings_value_index[SETTING_UNIT] == 1u) {
        lcd_format_count_rate(lcd_current_cpm);
        return;
    }
    if (rate_thousandths > LCD_RATE_MAX_VALUE) {
        rate_thousandths = LCD_RATE_MAX_VALUE;
    }
    /* Input is in 0.001 µSv/h (nSv/h), in units of 1/18 pSv. */
    lcd_format_scaled_sv((uint64_t)rate_thousandths * 18000u, 10u, false);
}

static void lcd_blank_large_value(void)
{
    for (uint8_t i = 0u; i < LCD_RATE_MASK_GLYPHS; ++i) {
        lcd_rate_glyphs[i] = ' ';
    }
    lcd_rate_glyphs[LCD_RATE_MASK_GLYPHS] = '\0';
    lcd_rate_glyph_count = LCD_RATE_MASK_GLYPHS;
}

static void lcd_format_rate_display(uint32_t rate_thousandths)
{
    lcd_format_rate(rate_thousandths);
    if (!lcd_rate_has_measurement) lcd_blank_large_value();
}

static void lcd_format_dose_display(uint32_t dose_thousandths,
                                    uint32_t dose_fraction)
{
    lcd_format_dose(dose_thousandths, dose_fraction);
    if (!lcd_dose_has_measurement) lcd_blank_large_value();
}

static void lcd_format_cpm(uint32_t cpm)
{
    const uint8_t row_mode = lcd_settings_value_index[SETTING_RATE_ROW_2];
    const uint8_t selected_unit = lcd_settings_value_index[SETTING_UNIT];

    for (uint8_t i = 0u; i < LCD_CPM_TEXT_LENGTH; ++i) {
        lcd_cpm_text[i] = ' ';
    }
    if ((row_mode < 2u && selected_unit == 1u) ||
        (row_mode == 3u && selected_unit != 1u)) {
        char saved_glyphs[sizeof(lcd_rate_glyphs)];
        char saved_label[LCD_RATE_LABEL_LENGTH];
        const uint8_t saved_count = lcd_rate_glyph_count;
        const uint8_t saved_unit = lcd_settings_value_index[SETTING_UNIT];
        const uint8_t saved_prefix = lcd_settings_value_index[SETTING_RATE_PREFIX];
        const uint32_t saved_rate = lcd_rate_value_thousandths;
        const char saved_display_prefix = lcd_display_unit_prefix;
        for (uint8_t i = 0u; i < sizeof(saved_glyphs); ++i) saved_glyphs[i] = lcd_rate_glyphs[i];
        for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) saved_label[i] = lcd_rate_label[i];
        lcd_settings_value_index[SETTING_UNIT] = selected_unit == 1u ?
            (row_mode == 0u ? 0u : 2u) : (selected_unit == 0u ? 2u : 0u);
        if (selected_unit == 1u) lcd_settings_value_index[SETTING_RATE_PREFIX] = 1u;
        lcd_format_rate(saved_rate);
        uint8_t position = 0u;
        while (position < LCD_CPM_TEXT_LENGTH && position < lcd_rate_glyph_count) {
            lcd_cpm_text[position] = lcd_rate_glyphs[position];
            ++position;
        }
        for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH &&
             position < LCD_CPM_TEXT_LENGTH; ++i) {
            if (lcd_rate_label[i] != ' ') lcd_cpm_text[position++] = lcd_rate_label[i];
        }
        for (uint8_t i = 0u; i < sizeof(saved_glyphs); ++i) lcd_rate_glyphs[i] = saved_glyphs[i];
        for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) lcd_rate_label[i] = saved_label[i];
        lcd_rate_glyph_count = saved_count;
        lcd_settings_value_index[SETTING_UNIT] = saved_unit;
        lcd_settings_value_index[SETTING_RATE_PREFIX] = saved_prefix;
        lcd_rate_value_thousandths = saved_rate;
        lcd_display_unit_prefix = saved_display_prefix;
    } else if (row_mode < 2u || (row_mode == 3u && selected_unit == 1u)) {
        if (row_mode == 1u || row_mode == 3u) {
            uint32_t cps_thousandths = (uint32_t)(
                ((uint64_t)cpm * 1000u + 30u) / 60u);
            uint32_t whole = cps_thousandths / 1000u;
            uint8_t reversed[3];
            uint8_t digits = 0u;
            do {
                reversed[digits++] = (uint8_t)('0' + whole % 10u);
                whole /= 10u;
            } while (whole != 0u && digits < sizeof(reversed));
            for (uint8_t i = 0u; i < digits; ++i) {
                lcd_cpm_text[3u - digits + i] =
                    (char)reversed[digits - i - 1u];
            }
            lcd_cpm_text[3] = '.';
            lcd_cpm_text[4] = (char)('0' +
                (cps_thousandths / 100u) % 10u);
            lcd_cpm_text[5] = (char)('0' +
                (cps_thousandths / 10u) % 10u);
            lcd_cpm_text[6] = (char)('0' + cps_thousandths % 10u);
            lcd_cpm_text[8] = 'C';
            lcd_cpm_text[9] = 'P';
            lcd_cpm_text[10] = 'S';
        } else {
            char reversed[LCD_CPM_MAX_DIGITS];
            uint8_t digits = 0u;
            if (cpm > 9999u) cpm = 9999u;
            do {
                reversed[digits++] = (char)('0' + cpm % 10u);
                cpm /= 10u;
            } while (cpm != 0u && digits < sizeof(reversed));
            for (uint8_t i = 0u; i < digits; ++i) {
                lcd_cpm_text[LCD_CPM_MAX_DIGITS - digits + i] =
                    reversed[digits - i - 1u];
            }
            lcd_cpm_text[5] = 'C';
            lcd_cpm_text[6] = 'P';
            lcd_cpm_text[7] = 'M';
        }
    }
    lcd_cpm_text[LCD_CPM_TEXT_LENGTH] = '\0';
}

static void lcd_format_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction)
{
    if (lcd_settings_value_index[SETTING_UNIT] == 1u) {
        lcd_format_count_value(lcd_daily_pulse_count, "CNT  ");
        return;
    }
    /* The whole part is in nSv; the remainder is in 1/3600 nSv.
     * One accumulator step is 5/18 pSv; keep that fraction exactly. */
    const uint64_t value_picosv_x18 =
        (uint64_t)dose_thousandths * 18000u +
        (uint64_t)dose_fraction * 5u;
    lcd_format_scaled_sv(value_picosv_x18, 11u, true);
}

static void lcd_format_dose_row(void)
{
    const uint8_t choice = lcd_dose_row_option(
        lcd_settings_value_index[SETTING_DOSE_ROW_2]);
    for (uint8_t i = 0u; i < LCD_CPM_TEXT_LENGTH; ++i) lcd_cpm_text[i] = ' ';
    if (choice == LCD_DOSE_ROW_OFF) {
        lcd_cpm_text[LCD_CPM_TEXT_LENGTH] = '\0';
        return;
    }
    if (choice == LCD_DOSE_ROW_TIME) {
        lcd_format_dose_uptime_row();
        return;
    }
    char saved_glyphs[sizeof(lcd_rate_glyphs)];
    char saved_label[LCD_RATE_LABEL_LENGTH];
    const uint8_t saved_count = lcd_rate_glyph_count;
    const uint8_t saved_unit = lcd_settings_value_index[SETTING_UNIT];
    const uint8_t saved_period = lcd_settings_value_index[SETTING_DOSE_PERIOD];
    const uint8_t saved_prefix = lcd_settings_value_index[SETTING_DOSE_PREFIX];
    const char saved_display_prefix = lcd_display_unit_prefix;
    const uint32_t saved_dose_count = lcd_daily_pulse_count;
    for (uint8_t i = 0u; i < sizeof(saved_glyphs); ++i) saved_glyphs[i] = lcd_rate_glyphs[i];
    for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) saved_label[i] = lcd_rate_label[i];

    if (choice == LCD_DOSE_ROW_CPM) {
        lcd_format_count_value(lcd_current_cpm, "CPM  ");
    } else {
        uint8_t row_unit = saved_unit;
        uint8_t row_period = saved_period;
        if (choice < 3u) {
            row_unit = choice;
        } else {
            row_period = (uint8_t)(choice - LCD_DOSE_ROW_PERIOD_BASE);
        }
        lcd_daily_pulse_count = lcd_dose_period_pulses[row_period];
        if (row_unit == 1u) {
            lcd_format_count_value(lcd_daily_pulse_count, "CNT  ");
        } else {
            const uint64_t scaled =
                (uint64_t)lcd_daily_pulse_count * 400u;
            const uint32_t dose_thousandths = (uint32_t)(scaled / 3600u);
            const uint32_t dose_fraction = (uint32_t)(scaled % 3600u);
            lcd_settings_value_index[SETTING_UNIT] = row_unit;
            lcd_format_dose(dose_thousandths, dose_fraction);
            lcd_rate_label[2] = ' ';
            lcd_rate_label[3] = ' ';
            lcd_rate_label[4] = ' ';
        }
    }

    uint8_t position = 0u;
    while (position < LCD_CPM_TEXT_LENGTH && position < lcd_rate_glyph_count) {
        lcd_cpm_text[position] = lcd_rate_glyphs[position];
        ++position;
    }
    if (position < LCD_CPM_TEXT_LENGTH) lcd_cpm_text[position++] = ' ';
    for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH &&
         position < LCD_CPM_TEXT_LENGTH; ++i) {
        if (lcd_rate_label[i] != ' ') lcd_cpm_text[position++] = lcd_rate_label[i];
    }

    /* The large dose display shares these buffers; restore it after row formatting. */
    for (uint8_t i = 0u; i < sizeof(saved_glyphs); ++i) lcd_rate_glyphs[i] = saved_glyphs[i];
    for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) lcd_rate_label[i] = saved_label[i];
    lcd_rate_glyph_count = saved_count;
    lcd_settings_value_index[SETTING_UNIT] = saved_unit;
    lcd_settings_value_index[SETTING_DOSE_PREFIX] = saved_prefix;
    lcd_display_unit_prefix = saved_display_prefix;
    lcd_daily_pulse_count = saved_dose_count;
    lcd_cpm_text[LCD_CPM_TEXT_LENGTH] = '\0';
}

static void lcd_format_dose_uptime_row(void)
{
    uint32_t seconds = board_uptime_seconds;
    for (uint8_t i = 0u; i < LCD_CPM_TEXT_LENGTH; ++i) {
        lcd_cpm_text[i] = ' ';
    }
    if (seconds < 172800u) {
        const uint32_t hours = seconds / 3600u;
        const uint32_t minutes = (seconds / 60u) % 60u;
        const uint32_t remaining = seconds % 60u;
        const uint8_t offset = 1u;
        lcd_cpm_text[offset] = (char)('0' + hours / 10u);
        lcd_cpm_text[offset + 1u] = (char)('0' + hours % 10u);
        lcd_cpm_text[offset + 2u] = ':';
        lcd_cpm_text[offset + 3u] = (char)('0' + minutes / 10u);
        lcd_cpm_text[offset + 4u] = (char)('0' + minutes % 10u);
        lcd_cpm_text[offset + 5u] = ':';
        lcd_cpm_text[offset + 6u] = (char)('0' + remaining / 10u);
        lcd_cpm_text[offset + 7u] = (char)('0' + remaining % 10u);
    } else {
        uint32_t days = seconds / 86400u;
        if (days > 999u) days = 999u;
        const uint32_t hours = (seconds / 3600u) % 24u;
        char reversed_days[3];
        uint8_t day_digits = 0u;
        do {
            reversed_days[day_digits++] = (char)('0' + days % 10u);
            days /= 10u;
        } while (days != 0u && day_digits < sizeof(reversed_days));
        uint8_t position = 0u;
        uint8_t content_length = (uint8_t)(day_digits + 5u);
        position = (uint8_t)((LCD_CPM_TEXT_LENGTH - content_length) / 2u);
        while (day_digits != 0u) {
            lcd_cpm_text[position++] = reversed_days[--day_digits];
        }
        lcd_cpm_text[position++] = 'D';
        lcd_cpm_text[position++] = ' ';
        lcd_cpm_text[position++] = (char)('0' + hours / 10u);
        lcd_cpm_text[position++] = (char)('0' + hours % 10u);
        lcd_cpm_text[position] = 'H';
    }
    lcd_cpm_text[LCD_CPM_TEXT_LENGTH] = '\0';
}

static bool lptim_wait_flag(uint32_t flag)
{
    uint32_t timeout = 100000u;
    while ((LPTIM_ISR & flag) == 0u && --timeout != 0u) { }
    return timeout != 0u;
}

static void backlight_force_gpio_on(void)
{
    LPTIM_CR = 0u;
    backlight_pwm_running = false;
    /* GPIO high is the observed fully-on level for the backlight. */
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << BOARD_LCD_BACKLIGHT_PIN;
    REG32(GPIOB_BASE + GPIO_MODER) =
        (REG32(GPIOB_BASE + GPIO_MODER) &
         ~(3u << (BOARD_LCD_BACKLIGHT_PIN * 2u))) |
        (1u << (BOARD_LCD_BACKLIGHT_PIN * 2u));
}

static bool lcd_queue_byte(uint8_t value, bool is_data)
{
    /* The last queued byte may have drained in this same service pass,
     * before lcd_spi_pump() gets another chance to reset the indices. */
    if (lcd_queue_head == lcd_queue_tail) {
        lcd_queue_head = 0u;
        lcd_queue_tail = 0u;
    }
    if (lcd_queue_tail >= LCD_QUEUE_CAPACITY) {
        lcd_spi_ready = false;
        return false;
    }
    lcd_queue[lcd_queue_tail].value = value;
    lcd_queue[lcd_queue_tail].is_data = is_data ? 1u : 0u;
    ++lcd_queue_tail;
    return true;
}

static void lcd_command(uint8_t command, const uint8_t *data, uint8_t length)
{
    if (!lcd_spi_ready || !lcd_queue_byte(command, false)) {
        return;
    }
    for (uint8_t i = 0u; i < length; ++i) {
        if (!lcd_queue_byte(data[i], true)) {
            return;
        }
    }
}

static void lcd_queue_window(const uint8_t x_window[4], const uint8_t y_window[4])
{
    lcd_command(0x2Au, x_window, 4u);
    lcd_command(0x2Bu, y_window, 4u);
    lcd_command(0x2Cu, NULL, 0u);
}

static __attribute__((noinline)) void lcd_submit_text_window(
    const uint8_t x_window[4], const uint8_t y_window[4])
{
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
    lcd_queue_window(x_window, y_window);
    lcd_text_row_commands_queued = true;
}

static void lcd_configure(void)
{
    lcd_spi_ready = true;

    /* Keep CS inactive until the stock PB7 board-enable settling interval
     * completes. LCD_Init then selects the panel before resetting it. */
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u;
    REG32(GPIOA_BASE + GPIO_MODER) = (REG32(GPIOA_BASE + GPIO_MODER) &
        ~(3u << 22u)) | (1u << 22u);
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u;

    /* PA8 reset output. */
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 8u;
    REG32(GPIOA_BASE + GPIO_MODER) = (REG32(GPIOA_BASE + GPIO_MODER) &
        ~(3u << 16u)) | (1u << 16u);
    /* Keep PB11 D/C high (data) while idle. */
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 11u;
    REG32(GPIOB_BASE + GPIO_MODER) = (REG32(GPIOB_BASE + GPIO_MODER) &
        ~(3u << 22u)) | (1u << 22u);
    /* Stock firmware uses SPI2: PB13=SCK and PB15=MOSI, AF0. */
    REG32(GPIOB_BASE + GPIO_BSRR) = (1u << (16u + 13u)) | (1u << (16u + 15u));
    REG32(GPIOB_BASE + GPIO_MODER) = (REG32(GPIOB_BASE + GPIO_MODER) &
        ~((3u << 26u) | (3u << 30u))) | (2u << 26u) | (2u << 30u);
    /* Match HAL_GPIO_Init in stock: GPIO_SPEED_FREQ_VERY_HIGH (3). */
    REG32(GPIOB_BASE + GPIO_OSPEEDR) = (REG32(GPIOB_BASE + GPIO_OSPEEDR) &
        ~((3u << 26u) | (3u << 30u))) | (3u << 26u) | (3u << 30u);
    REG32(GPIOB_BASE + GPIO_AFRH) &= ~((0xFu << 20u) | (0xFu << 28u));

    RCC_APB1ENR |= 1u << 14u; /* SPI2 clock */
    (void)RCC_APB1ENR;
    REG32(SPI2_BASE + SPI_CR1) = 0u;
    /* STM32L0 uses the legacy SPI block: 8-bit frames are DFF=0 in CR1;
     * CR2 has no DS/FRXTH fields. */
    REG32(SPI2_BASE + SPI_CR2) = 0u;
    /* Master, software NSS high, mode 0. DIV2 at the 32 MHz PCLK1 matches
     * the stock 16 MHz SPI clock. */
    REG32(SPI2_BASE + SPI_CR1) = (1u << 2u) | (1u << 8u) |
                                  (1u << 9u) | (1u << 6u);

    /* The stock app drives PB7 high and waits 100 ms before LCD_Init.
     * Its board-level function is not proven, but this stock state matters. */
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 7u;
    REG32(GPIOB_BASE + GPIO_MODER) = (REG32(GPIOB_BASE + GPIO_MODER) &
        ~(3u << 14u)) | (1u << 14u);
    lcd_init_deadline = board_millis + 100u;
    lcd_init_state = LCD_INIT_BOARD_ENABLE_DELAY;
}

static bool lcd_queue_drained(void)
{
    return lcd_queue_head == lcd_queue_tail;
}

static bool lcd_spi_send_byte(uint8_t value)
{
    uint32_t timeout = 100000u;
    while ((REG32(SPI2_BASE + SPI_SR) & (1u << 1u)) == 0u && --timeout != 0u) { }
    if (timeout == 0u) {
        return false;
    }

    *(volatile uint8_t *)(uintptr_t)(SPI2_BASE + SPI_DR) = value;
    timeout = 100000u;
    while ((REG32(SPI2_BASE + SPI_SR) & 1u) == 0u && --timeout != 0u) {
    }
    if (timeout == 0u) {
        return false;
    }
    (void)*(volatile uint8_t *)(uintptr_t)(SPI2_BASE + SPI_DR);

    timeout = 100000u;
    while ((REG32(SPI2_BASE + SPI_SR) & (1u << 7u)) != 0u && --timeout != 0u) { }
    return timeout != 0u;
}

static void lcd_spi_dma_start(const uint16_t *pixels, uint32_t pixel_count,
                              bool text_row)
{
    /* DMA1 channel 5 is SPI2_TX. Repeat the background pixel for the base
     * frame; increment through a short RGB565 row buffer for glyphs. */
    DMA1_CH5_CCR = 0u;
    DMA1_IFCR = 0xFu << 16u;
    DMA1_CSELR = (DMA1_CSELR & ~(0xFu << 16u)) | (2u << 16u);
    DMA1_CH5_CPAR = SPI2_BASE + SPI_DR;
    DMA1_CH5_CMAR = (uint32_t)(uintptr_t)pixels;
    DMA1_CH5_CNDTR = pixel_count;
    DMA1_CH5_CCR = (1u << 4u) | (1u << 8u) | (1u << 10u) |
                   (2u << 12u) | (text_row ? (1u << 7u) : 0u);
    REG32(SPI2_BASE + SPI_CR1) &= ~(1u << 6u);
    REG32(SPI2_BASE + SPI_CR1) |= 1u << 11u; /* DFF: 16-bit frames */
    REG32(SPI2_BASE + SPI_CR1) |= 1u << 6u;
    REG32(SPI2_BASE + SPI_CR2) |= 1u << 1u; /* TXDMAEN */
    DMA1_CH5_CCR |= 1u;
    lcd_dma_active = true;
    lcd_dma_deadline = board_millis + 100u;
    lcd_dma_is_text_row = text_row;
    lcd_dma_text_glyph_advance = text_row ? lcd_text_stream_advance : 0u;
}

static void lcd_text_stream_next(void)
{
    uint16_t remaining = (uint16_t)(lcd_text_stream_total - lcd_text_stream_offset);
    uint16_t count = remaining < LCD_WIDTH ? remaining : LCD_WIDTH;
    for (uint16_t i = 0u; i < count; ++i)
        lcd_text_dma_pixels[i] = lcd_text_palette[lcd_text_glyph[lcd_text_stream_offset + i]];
    lcd_text_stream_offset = (uint16_t)(lcd_text_stream_offset + count);
    lcd_spi_dma_start(lcd_text_dma_pixels, count, true);
}

static __attribute__((noinline)) void lcd_spi_pump(void)
{

    if (!lcd_spi_ready) {
        return;
    }

    if (lcd_dma_active) {
        uint32_t status = DMA1_ISR;
        if ((status & (1u << 19u)) != 0u) {
            DMA1_CH5_CCR = 0u;
            REG32(SPI2_BASE + SPI_CR2) &= ~(1u << 1u);
            lcd_dma_active = false;
            lcd_graph_input_locked = false;
            lcd_spi_error = true;
            lcd_spi_ready = false;
            return;
        }
        if ((status & (1u << 17u)) == 0u) {
            if (deadline_reached(lcd_dma_deadline)) {
                lcd_spi_error = true;
                lcd_spi_ready = false;
            }
            return;
        }
        DMA1_CH5_CCR = 0u;
        REG32(SPI2_BASE + SPI_CR2) &= ~(1u << 1u);
        {
            uint32_t timeout = 100000u;
            while ((REG32(SPI2_BASE + SPI_SR) & (1u << 7u)) != 0u &&
                   --timeout != 0u) { }
            if (timeout == 0u) {
                lcd_dma_active = false;
                lcd_dma_is_text_row = false;
                lcd_fill_active = false;
                lcd_text_active = false;
                lcd_graph_input_locked = false;
                lcd_spi_error = true;
                lcd_spi_ready = false;
                return;
            }
        }
        (void)*(volatile uint16_t *)(uintptr_t)(SPI2_BASE + SPI_DR);
        (void)REG32(SPI2_BASE + SPI_SR);
        if (lcd_dma_is_text_row && lcd_text_stream_offset < lcd_text_stream_total) {
            lcd_dma_active = false;
            lcd_text_stream_next();
            return;
        }
        DMA1_IFCR = 0xFu << 16u;
        REG32(SPI2_BASE + SPI_CR1) &= ~(1u << 6u);
        REG32(SPI2_BASE + SPI_CR1) &= ~(1u << 11u);
        REG32(SPI2_BASE + SPI_CR1) |= 1u << 6u;
        lcd_dma_active = false;
        if (lcd_dma_is_text_row) {
            lcd_text_glyph_index = (uint8_t)(lcd_text_glyph_index +
                lcd_dma_text_glyph_advance);
            lcd_text_row_commands_queued = false;
        } else {
            lcd_fill_active = false;
            lcd_text_active = true;
            lcd_text_glyph_index = 0u;
        }
        lcd_dma_is_text_row = false;
        lcd_dma_text_glyph_advance = 0u;
        REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u;
        return;
    }

    if (lcd_queue_head == lcd_queue_tail && lcd_queue_tail != 0u) {
        lcd_queue_head = 0u;
        lcd_queue_tail = 0u;
    }

    if (lcd_queue_head < lcd_queue_tail) {
        const lcd_queue_item_t item = lcd_queue[lcd_queue_head++];
        REG32(GPIOB_BASE + GPIO_BSRR) = item.is_data != 0u ?
            (1u << 11u) : (1u << (16u + 11u));
        if (!lcd_spi_send_byte(item.value)) {
            lcd_spi_error = true;
            lcd_spi_ready = false;
        }
        return;
    }

    if (lcd_fill_start_pending) {
        lcd_fill_start_pending = false;
        lcd_fill_active = true;
    }

    if (lcd_fill_active) {
        REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 11u; /* PB11 D/C data */
        lcd_spi_dma_start(&lcd_draw_pixel,
            LCD_WIDTH * (LCD_Y_OFFSET + LCD_HEIGHT - LCD_PANEL_Y_OFFSET),
            false);
    }
}

void *board_log_workspace(uint16_t bytes)
{
    if (bytes > sizeof(lcd_text_glyph)) return NULL;
    uint32_t deadline = board_millis + 200u;
    while (lcd_dma_active && lcd_spi_ready && !deadline_reached(deadline)) lcd_spi_pump();
    if (lcd_dma_active) return NULL;
    lcd_queue_head = lcd_queue_tail = 0u;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
    return lcd_text_glyph;
}

static void lcd_send_controller_config(void)
{
    /* Stock command order and payloads; delays remain in the init state machine.
     * Each entry is command, payload length, payload. */
    static const uint8_t config[] = {
        0xB1u, 3u, 0x01u, 0x2Cu, 0x2Du,
        0xB2u, 3u, 0x01u, 0x2Cu, 0x2Du,
        0xB3u, 6u, 0x01u, 0x2Cu, 0x2Du, 0x01u, 0x2Cu, 0x2Du,
        0xB4u, 1u, 0x07u,
        0xC0u, 3u, 0xA2u, 0x02u, 0x84u,
        0xC1u, 1u, 0xC5u,
        0xC2u, 2u, 0x0Au, 0x00u,
        0xC3u, 2u, 0x8Au, 0x2Au,
        0xC4u, 2u, 0x8Au, 0xEEu,
        0xC5u, 1u, 0x0Eu,
        0x20u, 0u, /* INVOFF */
        0x36u, 1u, 0xA8u,
        0x3Au, 1u, 0x05u,
        /* Stock 80 x 160 init window, before rotation. */
        0x2Au, 4u, 0x00u, 0x00u, 0x00u, 0x4Fu,
        0x2Bu, 4u, 0x00u, 0x00u, 0x00u, 0x9Fu,
        0x21u, 0u, /* INVON */
        0xE0u, 16u, 0x02u, 0x1Cu, 0x07u, 0x12u, 0x37u, 0x32u, 0x29u, 0x2Du,
                   0x29u, 0x25u, 0x2Bu, 0x39u, 0x00u, 0x01u, 0x03u, 0x10u,
        0xE1u, 16u, 0x03u, 0x1Du, 0x07u, 0x06u, 0x2Eu, 0x2Cu, 0x29u, 0x2Du,
                   0x2Eu, 0x2Eu, 0x37u, 0x3Fu, 0x00u, 0x00u, 0x02u, 0x10u
    };
    for (uint8_t offset = 0u; offset < sizeof(config); ) {
        const uint8_t command = config[offset++];
        const uint8_t length = config[offset++];
        lcd_command(command, &config[offset], length);
        offset = (uint8_t)(offset + length);
    }
}

void board_lcd_backlight_set(bool enabled)
{
    if (enabled) {
        if (!backlight_requested_on) {
            /* Keep the rolling solar average across screen-off intervals. */
            lcd_backlight_ramp_last_ms = board_millis;
        }
        backlight_requested_on = true;
        uint32_t compare = ((uint32_t)backlight_brightness_percent *
                            BACKLIGHT_PWM_PERIOD) / 100u;
        if (compare >= BACKLIGHT_PWM_PERIOD) {
            compare = BACKLIGHT_PWM_PERIOD - 1u;
        }

        /* Preload the GPIO low/off level before connecting PWM output. */
        REG32(GPIOB_BASE + GPIO_BSRR) =
            1u << (16u + BOARD_LCD_BACKLIGHT_PIN);
        REG32(GPIOB_BASE + GPIO_AFRL) =
            (REG32(GPIOB_BASE + GPIO_AFRL) &
             ~(0xFu << (BOARD_LCD_BACKLIGHT_PIN * 4u))) |
            (2u << (BOARD_LCD_BACKLIGHT_PIN * 4u)); /* PB2=LPTIM1_OUT AF2 */
        REG32(GPIOB_BASE + GPIO_MODER) =
            (REG32(GPIOB_BASE + GPIO_MODER) &
             ~(3u << (BOARD_LCD_BACKLIGHT_PIN * 2u))) |
            (2u << (BOARD_LCD_BACKLIGHT_PIN * 2u));

        if (!backlight_pwm_running) {
            LPTIM_CR = 0u;
            LPTIM_CFGR = 1u << 21u; /* APB /1, PWM mode, inverted waveform */
            LPTIM_CR = 1u;   /* enable peripheral */
            LPTIM_ICR = (1u << 3u) | (1u << 4u);
            LPTIM_ARR = BACKLIGHT_PWM_PERIOD - 1u;
            if (!lptim_wait_flag(1u << 4u)) {
                backlight_force_gpio_on();
                return;
            }
            LPTIM_ICR = 1u << 4u;
            LPTIM_CMP = compare;
            if (!lptim_wait_flag(1u << 3u)) {
                backlight_force_gpio_on();
                return;
            }
            LPTIM_ICR = 1u << 3u;
            LPTIM_CR |= 1u << 2u; /* continuous counter start */
            backlight_pwm_running = true;
        } else {
            LPTIM_ICR = 1u << 3u;
            LPTIM_CMP = compare;
            if (!lptim_wait_flag(1u << 3u)) {
                backlight_force_gpio_on();
                return;
            }
            LPTIM_ICR = 1u << 3u;
        }
    } else {
        backlight_requested_on = false;
        REG32(GPIOB_BASE + GPIO_BSRR) =
            1u << (16u + BOARD_LCD_BACKLIGHT_PIN);
        REG32(GPIOB_BASE + GPIO_MODER) =
            (REG32(GPIOB_BASE + GPIO_MODER) &
             ~(3u << (BOARD_LCD_BACKLIGHT_PIN * 2u))) |
            (1u << (BOARD_LCD_BACKLIGHT_PIN * 2u));
        LPTIM_CR = 0u;
        backlight_pwm_running = false;
    }
}

void board_lcd_backlight_set_brightness(uint8_t percent)
{
    if (percent > 100u) {
        percent = 100u;
    }
    backlight_brightness_percent = percent;
    if (backlight_requested_on) {
        board_lcd_backlight_set(true);
    }
}

static void lcd_begin_fill(void)
{
    uint8_t x_window[4] = {0u, 0u, 0u, (uint8_t)(LCD_WIDTH - 1u)};
    uint8_t y_window[4] = {0u, LCD_PANEL_Y_OFFSET, 0u,
        (uint8_t)(LCD_Y_OFFSET + LCD_HEIGHT - 1u)};

    lcd_draw_pixel = LCD_TEXT_BACKGROUND;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_text_glyph_index = 0u;
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u); /* CS low */
    lcd_queue_window(x_window, y_window);
    if (!lcd_spi_ready) {
        lcd_init_state = LCD_INIT_FAILED;
        return;
    }
    lcd_fill_active = false;
    lcd_fill_start_pending = true;
}

static void lcd_top_row_draw_glyph(char glyph, uint16_t x_start,
                                  uint16_t row_width, uint16_t color)
{
    for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
        for (uint8_t x = 0u; x < FONT_SMALL_CELL_WIDTH; ++x) {
            const uint16_t pixel_x = (uint16_t)(x_start + x);
            if (pixel_x < row_width && font_small_pixel(glyph, x, y)) {
                lcd_glyph_put((uint16_t)y * row_width + pixel_x, color);
            }
        }
    }
}

static void lcd_top_row_draw_percent(uint8_t offset, char icon,
                                    uint8_t right_edge, uint16_t color)
{
    uint8_t first = offset;
    uint8_t count;
    uint16_t width;
    uint16_t x;

    while (first < offset + 3u && lcd_adc_glyphs[first] == ' ') {
        ++first;
    }
    count = (uint8_t)(offset + 4u - first + 1u);
    width = (uint16_t)count * FONT_SMALL_CELL_WIDTH +
        (uint16_t)(count - 1u) * LCD_SMALL_TEXT_GAP;
    x = (uint16_t)(right_edge + 1u - width);

    lcd_top_row_draw_glyph(icon, x, LCD_WIDTH, LCD_TEXT_FOREGROUND);
    x = (uint16_t)(x + FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP);
    for (uint8_t i = first; i < offset + 4u; ++i) {
        lcd_top_row_draw_glyph(lcd_adc_glyphs[i], x, LCD_WIDTH, color);
        x = (uint16_t)(x + FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP);
    }
}

static void lcd_top_row_draw_uptime(void)
{
    const uint32_t total_minutes = board_uptime_seconds / 60u;
    rtc_calendar_t calendar;
    const bool show_rtc = rtc_time_valid && rtc_read_calendar(&calendar);
    const uint8_t hour = show_rtc ? calendar.hour :
        (uint8_t)((total_minutes / 60u) % 24u);
    const uint8_t minute = show_rtc ? calendar.minute :
        (uint8_t)(total_minutes % 60u);
    const char time_text[5] = {
        (char)('0' + hour / 10u),
        (char)('0' + hour % 10u),
        ':',
        (char)('0' + minute / 10u),
        (char)('0' + minute % 10u)
    };

    for (uint8_t i = 0u; i < sizeof(time_text); ++i) {
        lcd_top_row_draw_glyph(time_text[i],
            (uint16_t)(4u + i * (FONT_SMALL_CELL_WIDTH +
                                 LCD_SMALL_TEXT_GAP)),
            LCD_WIDTH, LCD_TEXT_FOREGROUND);
    }
}

static void lcd_top_row_draw_mode_letter(void)
{
    if (lcd_indication_mode == 0u) {
        lcd_top_row_draw_glyph('I', 60u, LCD_WIDTH, LCD_TEXT_RED);
    } else if (lcd_indication_mode == 2u) {
        lcd_top_row_draw_glyph('S', 60u, LCD_WIDTH, LCD_TEXT_GREEN);
    }
}

static void lcd_top_row_draw_flashlight_letter(void)
{
    if (!flashlight_manual_enabled) return;
    lcd_top_row_draw_glyph('L', 68u, LCD_WIDTH, LCD_TEXT_YELLOW);
}

static uint8_t lcd_debug_u32_digits(char *text, uint32_t value)
{
    char reversed[10];
    uint8_t digits = 0u;

    do {
        reversed[digits++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && digits < sizeof(reversed));
    for (uint8_t i = 0u; i < digits; ++i) {
        text[i] = reversed[digits - i - 1u];
    }
    return digits;
}

static uint8_t lcd_debug_time_percent(char *text, uint32_t seconds)
{
    const uint32_t uptime = board_uptime_seconds;
    uint32_t hours = seconds / 3600u;
    const uint32_t minutes = (seconds / 60u) % 60u;
    uint32_t percent = uptime == 0u ? 0u : (uint32_t)(
        ((uint64_t)seconds * 100u + uptime / 2u) / uptime);

    text[0] = (char)('0' + percent / 100u);
    text[1] = (char)('0' + (percent / 10u) % 10u);
    text[2] = (char)('0' + percent % 10u);
    text[3] = '%';
    text[4] = ' ';
    text[5] = (char)('0' + (hours / 1000u) % 10u);
    text[6] = (char)('0' + (hours / 100u) % 10u);
    text[7] = (char)('0' + (hours / 10u) % 10u);
    text[8] = (char)('0' + hours % 10u);
    text[9] = ':';
    text[10] = (char)('0' + minutes / 10u);
    text[11] = (char)('0' + minutes % 10u);
    return 12u;
}

static void lcd_debug_put_right(char *line, uint8_t end, const char *value,
                                uint8_t length)
{
    uint8_t start = length < end ? (uint8_t)(end - length) : 0u;
    for (uint8_t i = 0u; i < length && start + i < end; ++i) {
        line[start + i] = value[i];
    }
}

static uint8_t lcd_info_make_line(uint8_t row, char *line)
{
    char value[20];
    uint8_t value_length = 0u;
    const char *label;
    const uint8_t subpage = lcd_debug_subpage;
    for (uint8_t i = 0u; i < 20u; ++i) line[i] = ' ';

    if (subpage == 0u) {
        const uint32_t seconds = board_uptime_seconds;
        if (row == 1u) {
            rtc_calendar_t calendar;
            if (rtc_time_valid && rtc_read_calendar(&calendar)) {
                line[0] = (char)('0' + calendar.year / 1000u);
                line[1] = (char)('0' + (calendar.year / 100u) % 10u);
                line[2] = (char)('0' + (calendar.year / 10u) % 10u);
                line[3] = (char)('0' + calendar.year % 10u);
                line[4] = '-';
                line[5] = (char)('0' + calendar.month / 10u);
                line[6] = (char)('0' + calendar.month % 10u);
                line[7] = '-';
                line[8] = (char)('0' + calendar.day / 10u);
                line[9] = (char)('0' + calendar.day % 10u);
                line[12] = (char)('0' + calendar.hour / 10u);
                line[13] = (char)('0' + calendar.hour % 10u);
                line[14] = ':';
                line[15] = (char)('0' + calendar.minute / 10u);
                line[16] = (char)('0' + calendar.minute % 10u);
                line[17] = ':';
                line[18] = (char)('0' + calendar.second / 10u);
                line[19] = (char)('0' + calendar.second % 10u);
            } else {
                const char unset[] = "---- -- --  --:--:--";
                for (uint8_t i = 0u; i < 20u; ++i) line[i] = unset[i];
            }
            return 20u;
        } else if (row == 2u) {
            label = "UPTIME";
            const uint32_t days = seconds / 86400u;
            const uint32_t minutes = (seconds / 60u) % 1440u;
            value_length = lcd_debug_u32_digits(value, days);
            value[value_length++] = 'D'; value[value_length++] = ' ';
            value[value_length++] = (char)('0' + minutes / 600u);
            value[value_length++] = (char)('0' + (minutes / 60u) % 10u);
            value[value_length++] = ':';
            value[value_length++] = (char)('0' + (minutes % 60u) / 10u);
            value[value_length++] = (char)('0' + minutes % 10u);
            value[value_length++] = ':';
            value[value_length++] = (char)('0' + (seconds % 60u) / 10u);
            value[value_length++] = (char)('0' + seconds % 10u);
        } else if (row == 3u) {
            label = "CNT";
            value_length = lcd_debug_u32_digits(value, detector_pulse_count);
        } else if (row == 4u) {
            label = "RTC";
            const char *status = rtc_init_error == 1u ? "OSC ERR" :
                rtc_init_error == 2u ? "INIT ERR" :
                rtc_init_error == 3u ? "SYNC ERR" :
                rtc_init_error == 4u ? "DBP ERR" :
                !rtc_ready ? "NOT READY" :
                rtc_time_valid ? "SET" : "NOT SET";
            while (status[value_length] != '\0') {
                value[value_length] = status[value_length];
                ++value_length;
            }
            const char *clock_source = ((RCC_CSR >> 16u) & 3u) == 1u ? "LSE" :
                "LSI";
            value[value_length++] = ' ';
            value[value_length++] = clock_source[0];
            value[value_length++] = clock_source[1];
            value[value_length++] = clock_source[2];
        } else if (row == 5u) {
            label = "SPI";
            const uint32_t bytes = spi_nor_size_bytes();
            if (bytes == 0u) {
                value[0] = 'E'; value[1] = 'R'; value[2] = 'R';
                value_length = 3u;
            } else if (bytes >= 1024u * 1024u) {
                value_length = lcd_debug_u32_digits(value,
                    bytes / (1024u * 1024u));
                value[value_length++] = ' ';
                value[value_length++] = 'M'; value[value_length++] = 'B';
            } else {
                value_length = lcd_debug_u32_digits(value, bytes / 1024u);
                value[value_length++] = ' ';
                value[value_length++] = 'K'; value[value_length++] = 'B';
            }
        } else if (row == 6u) {
            static const char hex[] = "0123456789ABCDEF";
            const spi_nor_jedec_id_t id = spi_nor_jedec_id();
            const uint8_t bytes[3] = {
                id.manufacturer, id.memory_type, id.capacity
            };
            label = "JEDEC";
            if (bytes[0] == 0u && bytes[1] == 0u && bytes[2] == 0u) {
                value[value_length++] = '-';
            } else {
                for (uint8_t i = 0u; i < 3u; ++i) {
                    value[value_length++] = hex[bytes[i] >> 4u];
                    value[value_length++] = hex[bytes[i] & 0x0Fu];
                    if (i != 2u) value[value_length++] = ' ';
                }
            }
        } else {
            label = "";
        }
        for (uint8_t i = 0u; label[i] != '\0' && i < 12u; ++i) {
            line[i] = label[i];
        }
        lcd_debug_put_right(line, 20u, value, value_length);
    } else if (subpage == 2u) {
        static const char *const labels[] = {"MIN", "HOUR", "DAY", "ALERT", "SEC", "STORE"};
        if (row >= 1u && row <= 6u) {
            label = labels[row - 1u];
            if (row <= 4u) value_length = lcd_debug_u32_digits(value, log_history_count((uint8_t)(row - 1u)));
            else if (row == 5u) value_length = lcd_debug_u32_digits(value, second_log_count());
            else {
                const char *status = log_writer_fault() ? "ERR" : (settings_log_memory == BOARD_LOG_MEMORY_OFF ? "OFF" : "OK");
                while (status[value_length]) { value[value_length] = status[value_length]; ++value_length; }
            }
        }
    } else {
        if (row < 1u || row > 6u) return 20u;
        if (row <= 3u) {
            const uint16_t raw = row == 1u ? lcd_adc_battery_raw :
                (row == 2u ? lcd_adc_battery_start_raw : lcd_adc_sun_raw);
            const uint16_t percent = row == 1u ?
                lcd_displayed_battery_percent : (row == 2u ?
                    lcd_battery_start_percent : lcd_displayed_solar_percent);
            label = row == 1u ? "BATT" :
                (row == 2u ? "BATT START" : "SOL");
            for (uint8_t i = 0u; label[i] != '\0'; ++i) line[i] = label[i];
            value_length = lcd_debug_u32_digits(value, percent);
            value[value_length++] = '%';
            lcd_debug_put_right(line, 16u, value, value_length);
            value_length = lcd_debug_u32_digits(value, raw);
            lcd_debug_put_right(line, 20u, value, value_length);
        } else {
            const uint32_t duration = row == 4u ? lcd_debug_usb_seconds :
                (row == 5u ? lcd_debug_screen_seconds :
                             lcd_debug_sleep_seconds);
            label = row == 4u ? "CHARGE" :
                (row == 5u ? "SCREEN" : "SLEEP");
            for (uint8_t i = 0u; label[i] != '\0'; ++i) line[i] = label[i];
            value_length = lcd_debug_time_percent(value, duration);
            lcd_debug_put_right(line, 20u, value, value_length);
        }
        return 20u;
    }

    for (uint8_t i = 0u; label[i] != '\0' && i < 12u; ++i) line[i] = label[i];
    lcd_debug_put_right(line, 20u, value, value_length);
    return 20u;
}

#define LCD_SETTINGS_COUNT SETTING_COUNT
#define LCD_SETTINGS_MENU_COUNT SETTING_COUNT
#define LCD_SETTINGS_VISIBLE_ROWS 6u
#define LCD_SETTINGS_POWER_OFF_ITEM SETTING_POWER_OFF
#define LCD_SETTINGS_REBOOT_ITEM SETTING_REBOOT
#define LCD_CLEANUP_MENU_ACTIVE 0u
#define LCD_CLEANUP_CONFIRM_PENDING 0u
#define LCD_CLEANUP_COUNT 2u
#define LCD_RESET_COUNT 6u

static const uint8_t lcd_settings_item_order[LCD_SETTINGS_MENU_COUNT] = {
    23u, 0u, 1u, 2u, 3u, 4u, 16u, 24u, 25u, 26u, 27u, 17u,
    12u, 22u, 28u, 5u, 6u, 7u, 20u, 21u, 9u, 10u, 11u, 8u,
    15u, 13u, 14u, 18u, 19u
};

static bool lcd_settings_item_is_visible(uint8_t item)
{
    return item < LCD_SETTINGS_COUNT &&
        (lcd_settings_hidden_mask & (1UL << item)) == 0u &&
        !(lcd_settings_value_index[SETTING_UNIT] == 1u && item == 11u);
}

static uint8_t lcd_settings_count_visible_for_mask(uint32_t hidden_mask)
{
    uint8_t count = 0u;
    for (uint8_t i = 0u; i < LCD_SETTINGS_MENU_COUNT; ++i) {
        const uint8_t item = lcd_settings_item_order[i];
        if ((hidden_mask & (1UL << item)) == 0u &&
            !(lcd_settings_value_index[SETTING_UNIT] == 1u && item == 11u)) {
            ++count;
        }
    }
    return count;
}

static bool lcd_settings_has_visible_non_dose_prefix(uint32_t hidden_mask)
{
    for (uint8_t i = 0u; i < LCD_SETTINGS_MENU_COUNT; ++i) {
        const uint8_t item = lcd_settings_item_order[i];
        if (item != SETTING_DOSE_PREFIX &&
            (hidden_mask & (1UL << item)) == 0u) return true;
    }
    return false;
}

static const char *const lcd_cleanup_labels[LCD_CLEANUP_COUNT] = {
    "SETTINGS", "EEPROM"
};

static const char *const lcd_settings_labels[LCD_SETTINGS_COUNT] = {
    "LED", "CLICKS", "RATE SCREEN", "RATE LIGHT", "RATE SOUND", "LED BRIGHT",
    "SCREEN BRIGHT", "SCREEN TIMER", "LEAD ZERO", "UNIT", "RATE PREFIX",
    "DOSE PREF", "RATE ROW 2", "LOG MEMORY", "RESET", "HV", "RATE CUSTOM",
    "ALARM STOP", "POWER OFF", "REBOOT", "TONE", "LONG PRESS",
    "DOSE ROW 2", "DOSE PERIOD", "DOSE SCREEN", "DOSE LIGHT",
    "DOSE SOUND", "DOSE CUSTOM", "LED TIME"
};

/* Share value lists instead of reserving eleven pointers for every item. */
static const char *const indication_values[] = {
    "OFF", "ON", "ALWAYS", "YELLOW", "RED", "CUSTOM"
};
static const char *const scale_values[] = {
    "AUTO", "1", "3", "5", "10", "25", "50", "80", "100"
};
static const char *const timer_values[] = { "1", "5", "10", "30", "60", "90" };
static const char *const zero_values[] = { "SHOW", "HIDE" };
static const char *const unit_values[] = { "SV", "CNT", "R" };
static const char *const prefix_values[] = { "m", "\xB5", "n", "AUTO", "CPM", "CPS" };
static const char *const dose_period_values[] = { "UPTIME", "DAY", "TOTAL" };
static const char *const empty_values[] = { "" };
static const char *const reset_values[] = {
    "SETTINGS", "LOGS", "OLD LOGS", "EEPROM", "SPI", "ALL"
};
static const char *const dose_alarm_values[] = {
    "5", "7", "10", "30", "50", "100", "500", "1000"
};
static const char *const rate_custom_values[] = {
    "0.3", "0.4", "0.5", "0.7", "0.9", "1", "2", "5", "10"
};
static const char *const stop_values[] = { "CLEAR", "BUTTON" };
static const char *const tone_values[] = { "STOCK", "CLASSIC" };
static const char *const long_press_values[] = {
    "300MS", "500MS", "700MS", "900MS", "1100MS", "1300MS"
};
static const uint16_t long_press_milliseconds[] = {
    300u, 500u, 700u, 900u, 1100u, 1300u
};

static uint8_t lcd_dose_row_option(uint8_t option_index)
{
    const uint8_t selected_unit = lcd_settings_value_index[SETTING_UNIT];
    const uint8_t selected_period = lcd_settings_value_index[SETTING_DOSE_PERIOD];
    if (option_index == 0u) return LCD_DOSE_ROW_OFF;
    --option_index;
    for (uint8_t unit = 0u; unit < 3u; ++unit) {
        if (unit == selected_unit) continue;
        if (option_index == 0u) return unit;
        --option_index;
    }
    if (selected_period == 0u) {
        if (option_index == 0u) return LCD_DOSE_ROW_TIME;
        --option_index;
        for (uint8_t period = 1u; period < 3u; ++period) {
            if (option_index == 0u) return (uint8_t)(LCD_DOSE_ROW_PERIOD_BASE + period);
            --option_index;
        }
    } else {
        for (uint8_t period = 0u; period < 3u; ++period) {
            if (period == selected_period) continue;
            if (option_index == 0u) return (uint8_t)(LCD_DOSE_ROW_PERIOD_BASE + period);
            --option_index;
        }
        if (option_index == 0u) return LCD_DOSE_ROW_TIME;
        --option_index;
    }
    if (option_index == 0u) return LCD_DOSE_ROW_CPM;
    return LCD_DOSE_ROW_OFF;
}

static uint8_t lcd_dose_row_option_count(void)
{
    return 7u;
}

static const char *const *const lcd_settings_value_lists[] = {
    indication_values, scale_values, timer_values, zero_values, unit_values,
    prefix_values, empty_values, reset_values, rate_custom_values, stop_values,
    tone_values, long_press_values, dose_period_values, dose_alarm_values
};

static const uint8_t lcd_settings_value_list_map[(LCD_SETTINGS_COUNT + 1u) / 2u] = {
    0x00u, 0x00u, 0x10u, 0x21u, 0x43u, 0x55u, 0x76u, 0x07u,
    0x98u, 0x66u, 0xBAu, 0xC6u, 0x00u, 0xD0u, 0x01u
};

static const uint8_t lcd_settings_value_counts[LCD_SETTINGS_COUNT] = {
    6u, 6u, 4u, 4u, 4u, 9u, 9u, 6u, 2u, 3u, 6u, 6u, 3u, 3u, 6u, 2u,
    9u, 2u, 1u, 1u, 2u, 6u, 7u, 3u, 4u, 4u, 4u, 8u, 9u
};

static __attribute__((noinline)) uint8_t lcd_settings_value_count(uint8_t item)
{
    if (item == 10u || item == 11u) {
        return lcd_settings_value_index[SETTING_UNIT] == 1u ? 2u : 4u;
    }
    if (item == SETTING_RATE_ROW_2) {
        return 4u;
    }
    if (item == SETTING_DOSE_ROW_2) return lcd_dose_row_option_count();
    return lcd_settings_value_counts[item];
}

static const char *const *lcd_settings_value_list(uint8_t item)
{
    uint8_t packed = lcd_settings_value_list_map[item >> 1u];
    if ((item & 1u) != 0u) packed >>= 4u;
    return lcd_settings_value_lists[packed & 0x0Fu];
}

static const char *lcd_settings_value_text(uint8_t item)
{
    uint8_t value_index = lcd_settings_value_index[item];
    if (item == SETTING_USE_SPI && lcd_settings_spi_failed) return "FAILED";
    if (lcd_settings_editing && item == lcd_settings_selected) {
        value_index = lcd_settings_edit_value;
    }
    if (item == SETTING_RESET && LCD_CLEANUP_CONFIRM_PENDING) {
        return (value_index == 1u || value_index == 4u || value_index == 5u) ?
            "ERASE SPI" : "SURE?";
    }
    if (item == SETTING_RESET && lcd_cleanup_status != 0u) {
        return lcd_cleanup_status == 1u ? "SUCCESS" :
            (lcd_cleanup_status == 2u ? "FAILED" : "WORKING..." );
    }
    if (item == SETTING_USE_SPI) {
        return value_index < 2u ? reset_values[value_index + 3u] :
            indication_values[0u];
    }
    if ((item >= SETTING_RATE_SCREEN && item <= SETTING_RATE_SOUND) ||
        (item >= SETTING_DOSE_SCREEN && item <= SETTING_DOSE_SOUND)) {
        return indication_values[value_index == 0u ? 0u :
            (uint8_t)(value_index + 2u)];
    }
    if (item == SETTING_HV)
        return indication_values[value_index == 0u ? 1u : 0u];
    if ((item == 10u || item == 11u) &&
        lcd_settings_value_index[SETTING_UNIT] == 1u) {
        return prefix_values[value_index + 4u];
    }
    if (item == SETTING_RATE_ROW_2) {
        if (lcd_settings_value_index[SETTING_UNIT] == 1u) {
            if (value_index == 0u) return unit_values[0u];
            if (value_index == 1u) return unit_values[2u];
            if (value_index == 3u) return prefix_values[5u];
        } else if (value_index == 3u) {
            return lcd_settings_value_index[SETTING_UNIT] == 0u ?
                unit_values[2u] : unit_values[0u];
        }
        if (value_index == 0u) return prefix_values[4u];
        if (value_index == 1u) return prefix_values[5u];
        return indication_values[0u];
    }
    if (item == SETTING_DOSE_ROW_2) {
        const uint8_t option = lcd_dose_row_option(value_index);
        if (option == LCD_DOSE_ROW_OFF) return "OFF";
        if (option == LCD_DOSE_ROW_TIME) return "TIME";
        if (option == LCD_DOSE_ROW_CPM) return "CPM";
        if (option < 3u) return unit_values[option];
        if (option < 6u) return dose_period_values[option - LCD_DOSE_ROW_PERIOD_BASE];
        return "OFF";
    }
    return lcd_settings_value_list(item)[value_index];
}

static uint8_t lcd_settings_value_suffix(uint8_t item, uint8_t value)
{
    if (value == 0u) return 0u;
    if (item == SETTING_LED_TIME) return 2u;
    return (item == SETTING_LED_BRIGHT ||
        item == SETTING_SCREEN_BRIGHT) ? 1u : 0u;
}

static uint8_t lcd_settings_visible_count(void)
{
    if (LCD_CLEANUP_MENU_ACTIVE) return LCD_CLEANUP_COUNT;
    return lcd_settings_count_visible_for_mask(lcd_settings_hidden_mask);
}

static uint8_t lcd_settings_visible_to_item(uint8_t visible_index)
{
    uint8_t visible = 0u;

    if (LCD_CLEANUP_MENU_ACTIVE) return visible_index;

    for (uint8_t i = 0u; i < LCD_SETTINGS_MENU_COUNT; ++i) {
        const uint8_t item = lcd_settings_item_order[i];
        if (!lcd_settings_item_is_visible(item)) continue;
        if (visible == visible_index) return item;
        ++visible;
    }
    return LCD_SETTINGS_COUNT;
}

static uint8_t lcd_settings_item_to_visible(uint8_t item)
{
    if (LCD_CLEANUP_MENU_ACTIVE) return item;
    const uint8_t visible_count = lcd_settings_visible_count();

    for (uint8_t i = 0u; i < visible_count; ++i) {
        if (lcd_settings_visible_to_item(i) == item) return i;
    }
    return 0u;
}

static uint8_t lcd_settings_first_visible_item(void)
{
    const uint8_t selected_visible = LCD_CLEANUP_MENU_ACTIVE ?
        lcd_cleanup_selected :
        lcd_settings_item_to_visible(lcd_settings_selected);
    uint8_t first = selected_visible > 2u ?
        (uint8_t)(selected_visible - 2u) : 0u;
    const uint8_t visible_count = lcd_settings_visible_count();
    const uint8_t last_first = visible_count > LCD_SETTINGS_VISIBLE_ROWS ?
        (uint8_t)(visible_count - LCD_SETTINGS_VISIBLE_ROWS) : 0u;

    return first > last_first ? last_first : first;
}

static const char *lcd_settings_choice_text(uint8_t option)
{
    const uint8_t previous = lcd_settings_edit_value;
    lcd_settings_edit_value = option;
    const char *text = lcd_settings_value_text(lcd_settings_selected);
    lcd_settings_edit_value = previous;
    return text;
}

static __attribute__((noinline)) void lcd_settings_choice_line(
    uint8_t row, char line[20], uint8_t *selected_start,
    uint8_t *selected_end)
{
    memset(line, ' ', 20u);
    *selected_start = UINT8_MAX;
    *selected_end = 0u;
    if (row == 0u) {
        const char *label = lcd_settings_labels[lcd_settings_selected];
        for (uint8_t i = 0u; i < 18u && label[i] != '\0'; ++i) {
            line[i] = label[i];
        }
        return;
    }
    const uint8_t count = lcd_settings_value_count(lcd_settings_selected);
    const uint8_t split = (uint8_t)((count + 1u) / 2u);
    for (uint8_t column = 0u; column < 2u; ++column) {
        if (column == 0u && row > split) continue;
        const uint8_t option = column == 0u ? (uint8_t)(row - 1u) :
            (uint8_t)(split + row - 1u);
        if (option >= count) continue;
        const char *value = lcd_settings_choice_text(option);
        const uint8_t suffix = lcd_settings_value_suffix(
            lcd_settings_selected, option);
        const uint8_t start = (uint8_t)(column * 10u);
        uint8_t value_length = 0u;
        line[start] = option == lcd_settings_edit_value ? '>' : ' ';
        while (value[value_length] != '\0' && value_length < 6u)
            ++value_length;
        for (uint8_t i = 0u; i < value_length; ++i) {
            line[start + 1u + i] = value[i];
        }
        if (suffix == 1u)
            line[start + 1u + value_length] = '%';
        else if (suffix == 2u) {
            line[start + 1u + value_length] = 'm';
            line[start + 2u + value_length] = 's';
        }
        if (option == lcd_settings_edit_value) {
            *selected_start = start;
            *selected_end = (uint8_t)(start + 8u);
        }
    }
}

static uint8_t lcd_settings_make_line(uint8_t visible_row, char *line)
{
    const uint8_t first = lcd_settings_first_visible_item();
    const uint8_t item = lcd_settings_visible_to_item(
        (uint8_t)(first + visible_row));
    const char *label;
    const char *value;
    uint8_t suffix;
    uint8_t label_length = 0u;
    uint8_t value_length = 0u;
    uint8_t raw_value_length;
    uint8_t suffix_length;
    uint8_t value_start;

    for (uint8_t i = 0u; i < 20u; ++i) line[i] = ' ';
    if (item >= LCD_SETTINGS_COUNT) return 20u;
    if (LCD_CLEANUP_MENU_ACTIVE) {
        if (item == lcd_cleanup_selected &&
            (lcd_cleanup_status == 1u || lcd_cleanup_status == 3u)) {
            const char *message = lcd_cleanup_status == 1u ? "SUCCESS" :
                "WORKING...";
            uint8_t length = 0u;
            while (message[length] != '\0') ++length;
            const uint8_t start = (uint8_t)((20u - length) / 2u);
            for (uint8_t i = 0u; i < length; ++i) {
                line[start + i] = message[i];
            }
            return 20u;
        }
        const char *value = "";
        if (LCD_CLEANUP_CONFIRM_PENDING && item == lcd_cleanup_selected) {
            const bool clears_spi = item == 1u || item == 2u || item == 4u ||
                item == 5u || item == 8u;
            value = !settings_spi_enabled && clears_spi ? "SPI OFF?" :
                "SURE?";
        } else if (lcd_cleanup_status != 0u &&
                   item == lcd_cleanup_selected) {
            value = lcd_cleanup_status == 1u ? "SUCCESS" :
                (lcd_cleanup_status == 2u ? "FAILED" : "WORKING...");
        }
        label = lcd_cleanup_labels[item];
        if (item != lcd_cleanup_selected ||
            (!LCD_CLEANUP_CONFIRM_PENDING && lcd_cleanup_status == 0u)) {
            value = "";
        }
        line[0] = LCD_CLEANUP_CONFIRM_PENDING &&
            item == lcd_cleanup_selected ? '!' :
            (LCD_CLEANUP_MENU_ACTIVE && item == lcd_cleanup_selected ? '>' :
             ' ');
        while (label[label_length] != '\0' && label_length < 13u) {
            ++label_length;
        }
        while (value[value_length] != '\0' && value_length < 10u) {
            ++value_length;
        }
        for (uint8_t i = 0u; i < label_length && i + 1u < 20u; ++i) {
            line[i + 1u] = label[i];
        }
        value_start = value_length < 20u ?
            (uint8_t)(20u - value_length) : 0u;
        if (LCD_CLEANUP_CONFIRM_PENDING && item == lcd_cleanup_selected &&
            value_start > 0u) {
            --value_start;
        }
        for (uint8_t i = 0u; i < value_length && value_start + i < 20u; ++i) {
            line[value_start + i] = value[i];
        }
        return 20u;
    }
    line[0] = lcd_settings_list_active &&
        item == lcd_settings_selected && !lcd_settings_editing ? '>' : ' ';
    label = lcd_settings_labels[item];
    value = lcd_settings_value_text(item);
    suffix = lcd_settings_value_suffix(item,
        lcd_settings_editing && item == lcd_settings_selected ?
            lcd_settings_edit_value : lcd_settings_value_index[item]);
    while (label[label_length] != '\0' && label_length < 13u) ++label_length;
    while (value[value_length] != '\0' && value_length < 9u) ++value_length;
    raw_value_length = value_length;
    suffix_length = suffix == 2u ? 2u : suffix;
    value_length = (uint8_t)(raw_value_length + suffix_length);
    for (uint8_t i = 0u; i < label_length && i + 1u < 20u; ++i) {
        line[i + 1u] = label[i];
    }
    value_start = value_length < 20u ? (uint8_t)(20u - value_length) : 0u;
    if (value_start <= label_length + 1u) {
        value_start = (uint8_t)(label_length + 2u);
    }
    for (uint8_t i = 0u; i < raw_value_length && value_start + i < 20u; ++i)
        line[value_start + i] = value[i];
    if (suffix == 1u && value_start + raw_value_length < 20u) {
        line[value_start + raw_value_length] = '%';
    } else if (suffix == 2u && value_start + raw_value_length + 1u < 20u) {
        line[value_start + raw_value_length] = 'm';
        line[value_start + raw_value_length + 1u] = 's';
    }
    return 20u;
}

static bool lcd_graph_stat_icon_pixel(uint8_t stat, uint8_t x, uint8_t y)
{
    static const uint8_t icon_rows[2][7] = {
        { 4u, 14u, 21u, 4u, 4u, 4u, 4u },
        { 4u, 4u, 4u, 4u, 21u, 14u, 4u }
    };
    if (stat == 0u) return font_small_pixel(lcd_graph_log_type ? 'D' : 'C', x, y);
    if (stat == 2u) return font_small_pixel('A', x, y);
    if (stat == 3u && lcd_graph_log_type) return false;
    if ((stat != 1u && stat != 3u) || x < 1u || x > 5u || y < 1u || y > 7u) return false;
    return (icon_rows[stat == 1u ? 0u : 1u][y - 1u] & (uint8_t)(1u << (5u - x))) != 0u;
}

static bool lcd_graph_stat_value_pixel(uint8_t stat, uint8_t x, uint8_t y)
{
    if (stat == 3u && lcd_graph_log_type && lcd_graph_stat_min >= 864000000u) {
        const uint8_t digit = x / 8u;
        return font_small_pixel(digit < 5u ? lcd_graph_stat_values[stat][digit] : 'd',
                                (uint8_t)(x % 8u), y);
    }
    if (x < 8u) return lcd_graph_stat_icon_pixel(stat, x, y);
    if (x < 48u) {
        const uint8_t rel = (uint8_t)(x - 8u);
        const uint8_t digit = rel / 8u;
        return digit < 5u && rel % 8u < 7u &&
            font_small_pixel(lcd_graph_stat_values[stat][digit],
                             (uint8_t)(rel % 8u), y);
    }
    return false;
}

static uint64_t lcd_graph_stat_rate(uint32_t cpm)
{
    uint64_t rate = ((uint64_t)cpm * 666u + 50u) / 100u;
    if (lcd_settings_value_index[SETTING_UNIT] == 2u) {
        rate *= 100u;
    }
    return rate;
}

static uint64_t lcd_graph_stat_whole(uint64_t rate, uint8_t unit)
{
    static const uint64_t divisors[3] = { 1000000u, 1000u, 1u };
    return unit < 3u ? rate / divisors[unit] : rate * 1000u;
}

static uint8_t lcd_graph_stat_unit(uint32_t maximum_cpm)
{
    uint8_t fallback = 3u;
    bool has_fallback = false;
    const uint8_t selected = lcd_settings_value_index[SETTING_RATE_PREFIX];

    /* In count mode graph samples are CPM, not a dose-rate unit. */
    if (lcd_settings_value_index[SETTING_UNIT] == 1u) return 4u;
    if (selected < 3u) return selected;
    const uint64_t rate = lcd_graph_stat_rate(maximum_cpm);
    for (uint8_t unit = 0u; unit < 4u; ++unit) {
        const uint64_t whole = lcd_graph_stat_whole(rate, unit);
        if (whole >= 10u && whole < 1000u) return unit;
        if (whole != 0u && !has_fallback) {
            fallback = unit;
            has_fallback = true;
        }
    }
    return fallback;
}

/* Shared five-cell number writer for graph rate, dose and raw counters. */
static void lcd_graph_format_overflow(char text[5])
{
    text[0] = '>';
    for (uint8_t i = 1u; i < 5u; ++i) text[i] = '9';
}

static void lcd_graph_format_number(uint32_t scaled, uint32_t factor,
                                     uint8_t decimals, char text[5])
{
    uint32_t integer = scaled / factor;
    if (integer > 99999u) {
        lcd_graph_format_overflow(text);
        return;
    }
    const uint32_t fraction = scaled % factor;
    char reversed[5];
    uint8_t digits = 0u, out = 0u;
    if (integer != 0u || decimals == 0u ||
        lcd_settings_value_index[SETTING_LEAD_ZERO] == 0u) {
        do {
            reversed[digits++] = (char)('0' + integer % 10u);
            integer /= 10u;
        } while (integer != 0u);
        while (digits != 0u) text[out++] = reversed[--digits];
    }
    if (decimals != 0u) {
        uint32_t divisor = factor / 10u;
        text[out++] = '.';
        for (uint8_t i = 0u; i < decimals; ++i) {
            text[out++] = (char)('0' + (fraction / divisor) % 10u);
            if (divisor > 1u) divisor /= 10u;
        }
    }
    while (out < 5u) text[out++] = ' ';
}

static void lcd_graph_set_auto_prefix(char text[5], uint8_t unit)
{
    static const char prefixes[4] = { 'm', (char)0xB5u, 'n', 'p' };
    if (unit < 4u) {
        uint8_t end = 4u;
        while (end != 0u && text[end] == ' ') --end;
        if (end < 4u) ++end;
        if (end != 0u && text[end - 1u] == '.') {
            text[end - 1u] = prefixes[unit];
            text[end] = ' ';
        } else {
            text[end] = prefixes[unit];
        }
    }
}

static void lcd_graph_format_stat(uint32_t cpm, uint8_t unit, char text[5])
{
    if (lcd_settings_value_index[SETTING_UNIT] == 1u) {
        lcd_graph_format_number(cpm, 1u, 0u, text);
        return;
    }
    uint64_t numerator = lcd_graph_stat_rate(cpm);
    uint64_t denominator = 1u;
    uint64_t factor = 1u;
    uint8_t decimal_limit = unit == 0u || unit == 3u ? 5u :
        (unit == 1u ?
            (lcd_settings_value_index[SETTING_UNIT] == 2u ? 1u : 3u) : 0u);
    if (unit == 0u) denominator = 1000000u;
    else if (unit == 1u) denominator = 1000u;
    else if (unit == 3u) numerator *= 1000u;
    const uint64_t whole = numerator / denominator;
    uint8_t decimals = whole == 0u ?
        (lcd_settings_value_index[SETTING_LEAD_ZERO] == 0u ? 3u : 4u) : 4u;
    if (whole != 0u) {
        uint8_t integer_digits = 1u;
        for (uint64_t n = whole; n >= 10u; n /= 10u) ++integer_digits;
        decimals = integer_digits < 5u ?
            (uint8_t)(5u - integer_digits - 1u) : 0u;
    }
    if (decimals > decimal_limit) decimals = decimal_limit;
    for (uint8_t i = 0u; i < decimals; ++i) factor *= 10u;
    uint64_t scaled = (numerator * factor + denominator / 2u) / denominator;
    while (decimals != 0u && scaled / factor >= 100000u) {
        --decimals;
        factor /= 10u;
        scaled = (numerator * factor + denominator / 2u) / denominator;
    }
    if (scaled > UINT32_MAX || factor > UINT32_MAX)
        lcd_graph_format_overflow(text);
    else lcd_graph_format_number((uint32_t)scaled, (uint32_t)factor,
                                 decimals, text);
    if (lcd_settings_value_index[SETTING_RATE_PREFIX] == 3u)
        lcd_graph_set_auto_prefix(text, unit);
}

static __attribute__((noinline)) void lcd_graph_format_dose(
    uint64_t count, char text[5], uint8_t unit_mode)
{
    if (unit_mode == 1u) {
        if (count > UINT32_MAX) lcd_graph_format_overflow(text);
        else lcd_graph_format_number((uint32_t)count, 1u, 0u, text);
        return;
    }
    uint64_t value = count * 1998u;
    if (lcd_settings_value_index[SETTING_UNIT] == 2u) value *= 100u;
    uint8_t unit = lcd_settings_value_index[SETTING_DOSE_PREFIX];
    if (unit > 2u) {
        uint8_t fallback = 3u;
        bool have_fallback = false;
        for (uint8_t i = 0u; i < 4u; ++i) {
            uint64_t whole = value / lcd_sv_unit_scales[i];
            if (whole >= 10u && whole < 1000u) { unit = i; break; }
            if (whole != 0u && !have_fallback) {
                fallback = i;
                have_fallback = true;
            }
            unit = fallback;
        }
    }
    uint64_t factor = 1u;
    const uint64_t whole = value / lcd_sv_unit_scales[unit];
    uint8_t integer_digits = 1u;
    for (uint64_t n = whole; n >= 10u; n /= 10u) ++integer_digits;
    const uint8_t decimals = whole == 0u ?
        (lcd_settings_value_index[SETTING_LEAD_ZERO] == 0u ? 4u : 3u) :
        (integer_digits < 4u ? (uint8_t)(4u - integer_digits) : 0u);
    for (uint8_t i = 0u; i < decimals; ++i) factor *= 10u;
    const uint64_t scaled = (value * factor + lcd_sv_unit_scales[unit] / 2u) /
        lcd_sv_unit_scales[unit];
    if (scaled > UINT32_MAX || factor > UINT32_MAX)
        lcd_graph_format_overflow(text);
    else lcd_graph_format_number((uint32_t)scaled, (uint32_t)factor,
                                 decimals, text);
    if (lcd_settings_value_index[SETTING_DOSE_PREFIX] == 3u)
        lcd_graph_set_auto_prefix(text, unit);
}

static void lcd_graph_format_duration(uint32_t seconds, char text[5])
{
    uint32_t value = seconds;
    char suffix = 's';
    if (seconds >= 10000u) {
        value = seconds / 60u;
        suffix = 'm';
        if (value >= 1000u) {
            value = seconds / 3600u;
            suffix = 'h';
            if (value >= 1000u) {
                value = seconds / 86400u;
                suffix = 'd';
            }
        }
    }
    const bool wide = value >= 10000u;
    text[0] = text[1] = text[2] = text[3] = ' ';
    text[4] = wide ? ' ' : suffix;
    for (uint8_t i = wide ? 5u : 4u;
         value != 0u && i != 0u; ) {
        text[--i] = (char)('0' + value % 10u);
        value /= 10u;
    }
}

static uint32_t lcd_graph_interpolate(uint32_t a, uint32_t b,
                                     uint32_t numerator, uint32_t denominator)
{
    const bool rising = b >= a;
    const uint32_t delta = rising ? b - a : a - b;
    const uint32_t part = (delta / denominator) * numerator +
        (((delta % denominator) * numerator + denominator / 2u) /
            denominator);
    return rising ? a + part : a - part;
}

static void lcd_graph_build_series(uint8_t requested)
{
    const uint8_t available = cpm_history_count();
    const uint8_t visible = available < requested ? available : requested;
    uint32_t maximum = 0u;
    uint32_t candidate;
    const uint16_t plot_top = LCD_GRAPH_TOP;
    const uint16_t plot_bottom = LCD_HEIGHT - 1u;
    const uint16_t plot_height = plot_bottom - plot_top + 1u;
    uint32_t minimum = UINT32_MAX;
    uint64_t sum = 0u;
    uint8_t stat_unit;

    for (uint16_t x = 0u; x < LCD_WIDTH; ++x) {
        lcd_graph_column_y[x] = UINT8_MAX;
    }
    for (uint8_t age = 0u; age < visible; ++age) {
        const bool got_value = cpm_history_get(age, &candidate);
        if (got_value) {
            if (candidate > maximum) maximum = candidate;
            if (candidate < minimum) minimum = candidate;
            sum += candidate;
        }
    }
    lcd_graph_stat_max = maximum;
    if (maximum == 0u) maximum = 1u;
    if (minimum == UINT32_MAX) minimum = 0u;
    lcd_graph_stat_min = minimum;
    const uint32_t average = visible == 0u ? 0u :
        (uint32_t)((sum + visible / 2u) / visible);
    stat_unit = lcd_graph_stat_unit(lcd_graph_stat_max);
    lcd_graph_format_stat(lcd_current_cpm, stat_unit,
        lcd_graph_stat_values[0]);
    lcd_graph_format_stat(lcd_graph_stat_max, stat_unit,
        lcd_graph_stat_values[1]);
    lcd_graph_format_stat(average, stat_unit,
        lcd_graph_stat_values[2]);
    lcd_graph_format_stat(lcd_graph_stat_min, stat_unit,
        lcd_graph_stat_values[3]);
    lcd_graph_yellow_threshold_y =
        LCD_GRAPH_YELLOW_THRESHOLD_CPM <= maximum ?
            (uint8_t)(plot_bottom -
                ((uint32_t)LCD_GRAPH_YELLOW_THRESHOLD_CPM *
                    (plot_height - 1u)) / maximum) : UINT8_MAX;
    lcd_graph_red_threshold_y = LCD_GRAPH_RED_THRESHOLD_CPM <= maximum ?
        (uint8_t)(plot_bottom -
            ((uint32_t)LCD_GRAPH_RED_THRESHOLD_CPM *
                (plot_height - 1u)) / maximum) : UINT8_MAX;
    if (visible == 0u) return;

    {
        /* Preserve the time scale while the requested history window fills.
         * Stretching a short startup history across the whole plot fabricates
         * a jump/valley that is not present in the samples. */
        const uint16_t data_start = visible >= requested ? 0u :
            (uint16_t)(LCD_GRAPH_PLOT_WIDTH - 1u -
                (((uint32_t)(visible - 1u) * (LCD_GRAPH_PLOT_WIDTH - 1u)) /
                    (requested - 1u)));
        const uint16_t span = (uint16_t)(LCD_GRAPH_PLOT_WIDTH - 1u -
            data_start);

        for (uint16_t plot_x = data_start; plot_x < LCD_GRAPH_PLOT_WIDTH;
             ++plot_x) {
            uint32_t value;
            if (visible == 1u || span == 0u) {
                (void)cpm_history_get(0u, &value);
            } else {
                const uint32_t position =
                    (uint32_t)(plot_x - data_start) * (visible - 1u);
                const uint8_t older_index = (uint8_t)(position / span);
                const uint32_t remainder = position % span;
                const uint8_t age = (uint8_t)(visible - 1u - older_index);
                uint32_t older_value = 0u;
                uint32_t newer_value = 0u;

                (void)cpm_history_get(age, &older_value);
                if (age != 0u) {
                    (void)cpm_history_get((uint8_t)(age - 1u),
                                          &newer_value);
                } else {
                    newer_value = older_value;
                }
                value = lcd_graph_interpolate(older_value, newer_value,
                    remainder, span);
            }
            lcd_graph_column_y[plot_x + LCD_GRAPH_STATS_WIDTH] =
                (uint8_t)(plot_bottom -
                ((uint64_t)value * (plot_height - 1u)) / maximum);
        }
    }
}

static void lcd_graph_select_period(bool forward)
{
    lcd_graph_log_type = (uint8_t)((lcd_graph_log_type + (forward ? 1u : 4u)) % 5u);
    lcd_graph_selected = 0u;
    lcd_graph_cursor_mode = false;
    lcd_graph_reset_log_window();
    lcd_graph_log_summary_valid = false;
    lcd_graph_loading = lcd_graph_log_type != 0u;
    lcd_graph_log_summary_work_active = false;
    lcd_graph_input_locked = true;
    lcd_graph_refresh_deadline = board_millis + 5000u;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = false;
    lcd_cpm_redraw_pending = true;
}

static uint8_t lcd_graph_log_record_limit(void)
{
    if (lcd_graph_log_type == 1u) return 60u;
    if (lcd_graph_log_type == 2u) return 24u;
    return 50u;
}

static bool lcd_graph_read_log_point(uint8_t index, log_point_t *point)
{
    if (index >= lcd_graph_log_available || point == NULL) return false;
    const uint8_t type = (uint8_t)(lcd_graph_log_type - 1u);
    const uint32_t period = type == LOG_RECORD_ALERT ? 60u :
        log_record_period_seconds((log_record_kind_t)type);
    log_cursor_t cursor = lcd_graph_log_record_cursors[index];
    return log_history_next(type, period, &cursor, 0u, point);
}

static bool lcd_graph_load_log_window(uint8_t start)
{
    if (start >= lcd_graph_log_available) return false;
    lcd_graph_log_window_start = start;
    lcd_graph_log_visible = 0u;
    while (lcd_graph_log_visible < LCD_GRAPH_VISIBLE_BARS &&
           (uint16_t)start + lcd_graph_log_visible < lcd_graph_log_available) {
        if (!lcd_graph_read_log_point(
                (uint8_t)(start + lcd_graph_log_visible),
                &lcd_graph_log_points[lcd_graph_log_visible])) break;
        ++lcd_graph_log_visible;
    }
    lcd_graph_log_window_valid = lcd_graph_log_visible != 0u;
    return lcd_graph_log_window_valid;
}

static bool lcd_graph_move_bar(bool older)
{
    if (!lcd_graph_log_summary_valid || lcd_graph_log_available == 0u)
        return false;
    if (!lcd_graph_log_window_valid && !lcd_graph_load_log_window(0u)) return false;
    if (lcd_graph_log_available == 0u) return false;

    const uint8_t old_record = lcd_graph_selected_record;
    const uint8_t old_window = lcd_graph_log_window_start;
    lcd_graph_previous_selected = lcd_graph_selected;
    if (older) {
        lcd_graph_selected_record = (uint8_t)(
            (lcd_graph_selected_record + 1u) % lcd_graph_log_available);
    } else {
        lcd_graph_selected_record = lcd_graph_selected_record == 0u ?
            (uint8_t)(lcd_graph_log_available - 1u) :
            (uint8_t)(lcd_graph_selected_record - 1u);
    }

    uint8_t max_start = lcd_graph_log_available > LCD_GRAPH_VISIBLE_BARS ?
        (uint8_t)(lcd_graph_log_available - LCD_GRAPH_VISIBLE_BARS) : 0u;
    uint8_t new_window = lcd_graph_selected_record > LCD_GRAPH_CURSOR_CENTER ?
        (uint8_t)(lcd_graph_selected_record - LCD_GRAPH_CURSOR_CENTER) : 0u;
    if (new_window > max_start) new_window = max_start;
    if (new_window != old_window) {
        if (new_window == (uint8_t)(old_window + 1u)) {
            for (uint8_t i = 0u; i + 1u < lcd_graph_log_visible; ++i)
                lcd_graph_log_points[i] = lcd_graph_log_points[i + 1u];
            lcd_graph_log_window_start = new_window;
            const uint8_t next_index = (uint8_t)(new_window +
                lcd_graph_log_visible - 1u);
            if (!lcd_graph_read_log_point(next_index,
                    &lcd_graph_log_points[lcd_graph_log_visible - 1u])) {
                lcd_graph_selected_record = old_record;
                lcd_graph_log_window_start = old_window;
                return false;
            }
        } else if (old_window == (uint8_t)(new_window + 1u)) {
            for (uint8_t i = lcd_graph_log_visible - 1u; i != 0u; --i)
                lcd_graph_log_points[i] = lcd_graph_log_points[i - 1u];
            lcd_graph_log_window_start = new_window;
            if (!lcd_graph_read_log_point(new_window,
                    &lcd_graph_log_points[0])) {
                lcd_graph_selected_record = old_record;
                lcd_graph_log_window_start = old_window;
                return false;
            }
        } else if (!lcd_graph_load_log_window(new_window)) {
            lcd_graph_selected_record = old_record;
            (void)lcd_graph_load_log_window(old_window);
            return false;
        }
        lcd_graph_selected = (uint8_t)(lcd_graph_selected_record -
            lcd_graph_log_window_start);
        lcd_graph_partial_kind = LCD_GRAPH_PARTIAL_WINDOW;
    } else {
        lcd_graph_selected = (uint8_t)(lcd_graph_selected_record - old_window);
        lcd_graph_partial_kind = LCD_GRAPH_PARTIAL_CURSOR;
    }
    lcd_graph_input_locked = true;
    lcd_graph_refresh_deadline = board_millis + 5000u;
    lcd_graph_partial_pending = true;
    return true;
}

static void lcd_graph_reset_log_window(void)
{
    lcd_graph_log_window_start = 0u;
    lcd_graph_selected_record = 0u;
    lcd_graph_selected = 0u;
    lcd_graph_log_visible = 0u;
    lcd_graph_log_available = 0u;
    lcd_graph_log_window_valid = false;
    lcd_graph_log_summary_valid = false;
    lcd_graph_loading = false;
    lcd_graph_log_summary_work_active = false;
    lcd_graph_log_summary_count = UINT8_MAX;
    lcd_graph_partial_pending = false;
    lcd_graph_partial_kind = 0u;
}

static __attribute__((noinline)) bool lcd_graph_update_log_summary(void)
{
    if (lcd_graph_log_summary_valid) return true;
    const uint8_t type = (uint8_t)(lcd_graph_log_type - 1u);
    const uint32_t period = type == LOG_RECORD_ALERT ? 60u :
        log_record_period_seconds((log_record_kind_t)type);
    if (lcd_graph_log_summary_count == UINT8_MAX) {
        lcd_graph_log_summary_count = 0u;
        lcd_graph_log_available = lcd_graph_log_record_limit();
        lcd_graph_log_summary_dose = 0u;
        lcd_graph_log_summary_coverage = 0u;
        lcd_graph_log_summary_max = 0u;
        lcd_graph_log_summary_bar_max = 0u;
        log_history_begin(&lcd_graph_log_summary_cursor);
    }
    uint8_t batch_end = (uint8_t)(lcd_graph_log_summary_count +
        LCD_GRAPH_LOG_BATCH_RECORDS);
    if (batch_end > lcd_graph_log_available)
        batch_end = lcd_graph_log_available;
    while (lcd_graph_log_summary_count < batch_end) {
        log_point_t point;
        const uint32_t index = lcd_graph_log_summary_count;
        lcd_graph_log_record_cursors[index] = lcd_graph_log_summary_cursor;
        if (!log_history_next(type, period, &lcd_graph_log_summary_cursor,
                0u, &point)) {
            lcd_graph_log_available = (uint8_t)index;
            break;
        }
        if (index < LCD_GRAPH_VISIBLE_BARS)
            lcd_graph_log_points[index] = point;
        lcd_graph_log_summary_dose += point.count;
        lcd_graph_log_summary_coverage += point.covered_seconds;
        if (point.maximum_cpm > lcd_graph_log_summary_max)
            lcd_graph_log_summary_max = point.maximum_cpm;
        if (point.count > lcd_graph_log_summary_bar_max)
            lcd_graph_log_summary_bar_max = point.count;
        ++lcd_graph_log_summary_count;
    }
    if (lcd_graph_log_summary_count < lcd_graph_log_available)
        return false;
    lcd_graph_log_visible = lcd_graph_log_available < LCD_GRAPH_VISIBLE_BARS ?
        lcd_graph_log_available : LCD_GRAPH_VISIBLE_BARS;
    lcd_graph_log_summary_average = lcd_graph_log_summary_coverage ?
        (uint32_t)((lcd_graph_log_summary_dose * 60u +
            lcd_graph_log_summary_coverage / 2u) /
            lcd_graph_log_summary_coverage) : 0u;
    lcd_graph_log_window_valid = lcd_graph_log_visible != 0u;
    lcd_graph_log_summary_valid = true;
    return true;
}

static void lcd_graph_build_logs(void)
{
    uint8_t *values = lcd_text_glyph;
    uint32_t maximum = 1u;
    for (uint16_t x = 0u; x < LCD_WIDTH; ++x) {
        lcd_graph_column_y[x] = UINT8_MAX;
    }
    if (lcd_graph_loading || !lcd_graph_log_summary_valid) {
        memset(lcd_graph_stat_values, '-', sizeof(lcd_graph_stat_values));
        return;
    }
    if (!lcd_graph_log_window_valid) lcd_graph_log_visible = 0u;
    if (lcd_graph_selected >= lcd_graph_log_visible) lcd_graph_selected = 0u;
    for (uint8_t age = 0u; age < lcd_graph_log_visible; ++age) {
        const log_point_t *point = &lcd_graph_log_points[age];
        uint32_t packed = point->count & 0x7FFFFFFFu;
        if (point->partial) packed |= 0x80000000u;
        memcpy(&values[(uint16_t)age * sizeof(packed)], &packed, sizeof(packed));
    }
    if (lcd_graph_log_summary_bar_max > maximum)
        maximum = lcd_graph_log_summary_bar_max;
    for (uint8_t age = 0u; age < lcd_graph_log_visible; ++age) {
        uint32_t packed;
        memcpy(&packed, &values[(uint16_t)age * sizeof(packed)], sizeof(packed));
        const uint32_t count = packed & 0x7FFFFFFFu;
        const uint8_t y = (uint8_t)(LCD_GRAPH_BASELINE -
            ((uint64_t)count * (LCD_GRAPH_HEIGHT - 1u)) / maximum);
        const uint16_t x = (uint16_t)(LCD_GRAPH_STATS_WIDTH +
            (uint16_t)age * LCD_GRAPH_BAR_STEP);
        for (uint8_t pixel = 0u; pixel < FONT_SMALL_CELL_WIDTH; ++pixel) {
            lcd_graph_column_y[x + pixel] =
                (uint8_t)(y | ((packed & 0x80000000u) ? 0x80u : 0u));
        }
    }
    const log_point_t *selected = lcd_graph_log_visible == 0u ? NULL :
        &lcd_graph_log_points[lcd_graph_selected];
    lcd_graph_selected_time = selected == NULL ? 0u : selected->period_start;
    lcd_graph_selected_calendar = selected != NULL && selected->calendar_valid;
    lcd_graph_stat_max = lcd_graph_cursor_mode && selected ? selected->maximum_cpm :
        lcd_graph_log_summary_max;
    lcd_graph_stat_min = lcd_graph_cursor_mode && selected ? selected->covered_seconds :
        lcd_graph_log_summary_coverage;
    const uint32_t average = lcd_graph_cursor_mode && selected ?
        selected->average_cpm : lcd_graph_log_summary_average;
    lcd_graph_format_dose(lcd_graph_cursor_mode && selected ? selected->count :
        lcd_graph_log_summary_dose, lcd_graph_stat_values[0],
        lcd_settings_value_index[SETTING_UNIT]);
    const uint8_t unit = lcd_graph_stat_unit(lcd_graph_stat_max);
    lcd_graph_format_stat(lcd_graph_stat_max, unit, lcd_graph_stat_values[1]);
    lcd_graph_format_stat(average, unit,
        lcd_graph_stat_values[2]);
    lcd_graph_format_duration(lcd_graph_stat_min,
        lcd_graph_stat_values[3]);
    /* These bars show accumulated dose, so CPM threshold lines would use a
     * mismatched vertical scale. */
    lcd_graph_yellow_threshold_y = UINT8_MAX;
    lcd_graph_red_threshold_y = UINT8_MAX;
}

static uint16_t lcd_graph_log_pixel(uint16_t x, uint16_t y)
{
    static const uint8_t sy[4] = {
        LCD_GRAPH_TOP + 1u, LCD_GRAPH_TOP + 17u,
        LCD_GRAPH_TOP + 33u, LCD_GRAPH_TOP + 49u
    };
    const uint8_t gy = (uint8_t)y;
    if (x < LCD_GRAPH_STATS_WIDTH - 1u) {
        for (uint8_t stat = 0u; stat < 4u; ++stat) {
            if (gy < sy[stat] || gy >= sy[stat] + FONT_SMALL_CELL_HEIGHT)
                continue;
            const uint8_t yy = (uint8_t)(gy - sy[stat]);
            uint16_t color = LCD_TEXT_FOREGROUND;
            if (lcd_graph_log_type == 4u && stat == 1u) {
                if (lcd_graph_stat_max >= LCD_GRAPH_RED_THRESHOLD_CPM)
                    color = LCD_TEXT_RED;
                else if (lcd_graph_stat_max >= LCD_GRAPH_YELLOW_THRESHOLD_CPM)
                    color = LCD_TEXT_YELLOW;
            }
            return x < 48u && lcd_graph_stat_value_pixel(stat, (uint8_t)x, yy) ?
                color : LCD_TEXT_BACKGROUND;
        }
        return LCD_TEXT_BACKGROUND;
    }
    if (x == LCD_GRAPH_STATS_WIDTH - 1u) return LCD_GRAPH_DIM_COLOR;
    if (lcd_graph_loading) return LCD_TEXT_BACKGROUND;
    if (gy > LCD_GRAPH_BASELINE) {
        const uint16_t left = LCD_GRAPH_STATS_WIDTH +
            (uint16_t)lcd_graph_selected * LCD_GRAPH_BAR_STEP;
        return lcd_graph_cursor_mode && gy >= LCD_GRAPH_BASELINE + 3u &&
            x >= left && x < left + FONT_SMALL_CELL_WIDTH ?
                LCD_SETTINGS_SELECTED_COLOR : LCD_TEXT_BACKGROUND;
    }
    const uint8_t column = lcd_graph_column_y[x];
    const bool selected = lcd_graph_cursor_mode &&
        x >= LCD_GRAPH_STATS_WIDTH +
            (uint16_t)lcd_graph_selected * LCD_GRAPH_BAR_STEP &&
        x < LCD_GRAPH_STATS_WIDTH +
            (uint16_t)lcd_graph_selected * LCD_GRAPH_BAR_STEP +
            FONT_SMALL_CELL_WIDTH;
    const bool fill = column != UINT8_MAX && gy >= (column & 0x7Fu);
    if (gy == lcd_graph_red_threshold_y) return LCD_COUNTER_RED;
    if (gy == lcd_graph_yellow_threshold_y) return LCD_COUNTER_YELLOW;
    if (!fill) return LCD_TEXT_BACKGROUND;
    if (selected) return LCD_SETTINGS_SELECTED_COLOR;
    return (column & 0x80u) ? LCD_GRAPH_DIM_COLOR : LCD_GRAPH_COLOR;
}

static uint16_t lcd_graph_pixel(uint16_t x, uint16_t y)
{
    static const uint8_t sy[4] = {
        LCD_GRAPH_TOP + 1u, LCD_GRAPH_TOP + 17u,
        LCD_GRAPH_TOP + 33u, LCD_GRAPH_TOP + 49u
    };
    if (lcd_graph_log_type) return lcd_graph_log_pixel(x, y);
    if (x < LCD_GRAPH_STATS_WIDTH - 1u) {
        for (uint8_t stat = 0u; stat < 4u; ++stat) {
            if (y < sy[stat] || y >= sy[stat] + FONT_SMALL_CELL_HEIGHT)
                continue;
            const uint8_t yy = (uint8_t)(y - sy[stat]);
            const uint16_t color = stat == 0u ? lcd_rate_color :
                LCD_TEXT_FOREGROUND;
            return x < 48u && lcd_graph_stat_value_pixel(stat, (uint8_t)x, yy) ?
                color : LCD_TEXT_BACKGROUND;
        }
        return LCD_TEXT_BACKGROUND;
    }
    if (x == LCD_GRAPH_STATS_WIDTH - 1u) return LCD_GRAPH_DIM_COLOR;
    const uint8_t column = lcd_graph_column_y[x];
    if (y == lcd_graph_red_threshold_y) return LCD_COUNTER_RED;
    if (y == lcd_graph_yellow_threshold_y) return LCD_COUNTER_YELLOW;
    return column != UINT8_MAX && y >= column ?
        LCD_GRAPH_COLOR : LCD_TEXT_BACKGROUND;
}

static void lcd_graph_prepare_header(void)
{
    static const uint8_t seconds_table[4] = {10u, 30u, 60u, 90u};
    static const char *const titles[] = {"SEC", "MIN", "HOUR", "DAY", "ALRT"};
    const uint8_t seconds = seconds_table[lcd_graph_range_index];
    char line[20];
    for (uint8_t i = 0u; i < 20u; ++i) line[i] = ' ';
    if (!lcd_graph_log_type) {
        line[0] = (char)('0' + seconds / 10u);
        line[1] = (char)('0' + seconds % 10u);
        line[2] = 's';
    }
    const char *title = titles[lcd_graph_log_type];
    if (lcd_graph_log_type) {
        lcd_graph_build_logs();
        if (lcd_graph_loading) memcpy(line, "WAIT...", 7u);
    }
    else lcd_graph_build_series((uint8_t)(seconds * 2u));
    if (lcd_graph_log_type && lcd_graph_cursor_mode) {
        if (lcd_graph_selected_calendar) {
            rtc_calendar_t calendar;
            (void)rtc_calendar_from_seconds_since_2000(
                lcd_graph_selected_time, &calendar);
            if (lcd_graph_log_type == 1u) {
                /* MIN: DD hh:mm */
                line[0] = (char)('0' + calendar.day / 10u);
                line[1] = (char)('0' + calendar.day % 10u);
                line[3] = (char)('0' + calendar.hour / 10u);
                line[4] = (char)('0' + calendar.hour % 10u);
                line[5] = ':';
                line[6] = (char)('0' + calendar.minute / 10u);
                line[7] = (char)('0' + calendar.minute % 10u);
            } else if (lcd_graph_log_type == 2u) {
                /* HOUR: MM-DD hh */
                line[0] = (char)('0' + calendar.month / 10u);
                line[1] = (char)('0' + calendar.month % 10u);
                line[2] = '-';
                line[3] = (char)('0' + calendar.day / 10u);
                line[4] = (char)('0' + calendar.day % 10u);
                line[6] = (char)('0' + calendar.hour / 10u);
                line[7] = (char)('0' + calendar.hour % 10u);
            } else {
                /* DAY: YY-MM-DD */
                const uint8_t year = (uint8_t)(calendar.year % 100u);
                line[0] = (char)('0' + year / 10u);
                line[1] = (char)('0' + year % 10u);
                line[2] = '-';
                line[3] = (char)('0' + calendar.month / 10u);
                line[4] = (char)('0' + calendar.month % 10u);
                line[5] = '-';
                line[6] = (char)('0' + calendar.day / 10u);
                line[7] = (char)('0' + calendar.day % 10u);
            }
        } else {
            memcpy(line, "UP   ", 5u);
        }
    }
    line[12] = '<';
    line[14] = title[0];
    line[15] = title[1];
    line[16] = title[2];
    if (title[3]) line[17] = title[3];
    line[19] = '>';
    const uint8_t special_start = lcd_graph_cursor_mode ? 20u : 14u;
    const uint8_t special_end = lcd_graph_cursor_mode ? 19u : 17u;
    lcd_render_small_line(line, LCD_WIDTH, LCD_TEXT_FOREGROUND,
        20u, 19u, special_start, special_end, LCD_COUNTER_GREEN);
}

static bool lcd_graph_prepare_partial_glyph(void)
{
    const uint8_t stage = lcd_text_glyph_index;
    uint8_t x0 = 0u, x1 = LCD_WIDTH - 1u;
    uint8_t y0 = 0u, y1 = FONT_SMALL_CELL_HEIGHT - 1u;
    uint8_t x_window[4] = {0u, 0u, 0u, LCD_WIDTH - 1u};
    uint8_t y_window[4];
    bool header = stage == 0u;
    if (stage > 0u && lcd_graph_partial_kind == LCD_GRAPH_PARTIAL_CURSOR) {
        if (stage == 1u || stage == 2u) {
            uint8_t bar = stage == 1u ? lcd_graph_previous_selected :
                lcd_graph_selected;
            x0 = (uint8_t)(LCD_GRAPH_STATS_WIDTH + bar * LCD_GRAPH_BAR_STEP);
            x1 = (uint8_t)(x0 + FONT_SMALL_CELL_WIDTH - 1u);
            y0 = LCD_GRAPH_TOP;
            y1 = LCD_HEIGHT - 1u;
        } else {
            const uint8_t chunk = (uint8_t)(stage - 3u);
            if (chunk >= LCD_GRAPH_CHUNK_COUNT) goto done;
            x1 = LCD_GRAPH_STATS_WIDTH - 2u;
            y0 = (uint8_t)(LCD_GRAPH_TOP + chunk * LCD_GRAPH_CHUNK_HEIGHT);
            y1 = (uint8_t)(y0 + LCD_GRAPH_CHUNK_HEIGHT - 1u);
            if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1u;
        }
    } else if (stage > 0u && lcd_graph_partial_kind == LCD_GRAPH_PARTIAL_LIVE) {
        if (stage == 1u) {
            x0 = LCD_GRAPH_STATS_WIDTH;
            x1 = (uint8_t)(x0 + FONT_SMALL_CELL_WIDTH - 1u);
            y0 = LCD_GRAPH_TOP;
            y1 = LCD_HEIGHT - 1u;
        } else {
            const uint8_t chunk = (uint8_t)(stage - 2u);
            if (chunk >= LCD_GRAPH_CHUNK_COUNT) goto done;
            x1 = LCD_GRAPH_STATS_WIDTH - 2u;
            y0 = (uint8_t)(LCD_GRAPH_TOP + chunk * LCD_GRAPH_CHUNK_HEIGHT);
            y1 = (uint8_t)(y0 + LCD_GRAPH_CHUNK_HEIGHT - 1u);
            if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1u;
        }
    } else if (stage > 0u) {
        const uint8_t chunk = (uint8_t)(stage - 1u);
        if (chunk < LCD_GRAPH_CHUNK_COUNT) {
            x0 = LCD_GRAPH_STATS_WIDTH;
            x1 = LCD_WIDTH - 1u;
            y0 = (uint8_t)(LCD_GRAPH_TOP + chunk * LCD_GRAPH_CHUNK_HEIGHT);
            y1 = (uint8_t)(y0 + LCD_GRAPH_CHUNK_HEIGHT - 1u);
            if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1u;
        } else if (chunk < (uint8_t)(2u * LCD_GRAPH_CHUNK_COUNT)) {
            const uint8_t stat_chunk = (uint8_t)(chunk - LCD_GRAPH_CHUNK_COUNT);
            x1 = LCD_GRAPH_STATS_WIDTH - 2u;
            y0 = (uint8_t)(LCD_GRAPH_TOP + stat_chunk * LCD_GRAPH_CHUNK_HEIGHT);
            y1 = (uint8_t)(y0 + LCD_GRAPH_CHUNK_HEIGHT - 1u);
            if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1u;
        } else goto done;
    }
    lcd_text_dma_glyph_advance = 1u;
    if (header) {
        lcd_text_glyph_pixels = LCD_TOP_ROW_PIXELS;
        lcd_graph_prepare_header();
        y_window[1] = LCD_Y_OFFSET; y_window[3] = (uint8_t)(LCD_Y_OFFSET + FONT_SMALL_CELL_HEIGHT - 1u);
    } else {
        lcd_text_glyph_pixels = (uint16_t)(x1 - x0 + 1u) * (y1 - y0 + 1u);
        uint16_t pos = 0u;
        for (uint8_t y = y0; y <= y1; ++y) for (uint8_t x = x0; x <= x1; ++x)
            lcd_glyph_put(pos++, lcd_graph_pixel(x, y));
        x_window[0] = (uint8_t)(x0 >> 8); x_window[1] = (uint8_t)x0;
        x_window[2] = (uint8_t)(x1 >> 8); x_window[3] = (uint8_t)x1;
        y_window[1] = (uint8_t)(LCD_Y_OFFSET + y0);
        y_window[3] = (uint8_t)(LCD_Y_OFFSET + y1);
    }
    y_window[0] = 0u; y_window[2] = 0u;
    lcd_submit_text_window(x_window, y_window);
    return true;
done:
    lcd_text_active = false;
    lcd_graph_partial_pending = false;
    lcd_graph_partial_kind = 0u;
    lcd_graph_input_locked = false;
    return false;
}

static bool lcd_graph_prepare_glyph(void)
{
    if (lcd_graph_partial_kind != 0u) return lcd_graph_prepare_partial_glyph();
    const uint8_t step = lcd_text_glyph_index;
    uint8_t x_window[4] = { 0u, 0u, 0u, LCD_WIDTH - 1u };
    uint8_t y_window[4];
    if (step > LCD_GRAPH_CHUNK_COUNT) {
        lcd_text_active = false;
        lcd_graph_input_locked = false;
        return false;
    }
    lcd_text_dma_glyph_advance = 1u;
    if (step == 0u) {
        lcd_text_glyph_pixels = LCD_TOP_ROW_PIXELS;
        lcd_graph_prepare_header();
        y_window[1] = LCD_Y_OFFSET;
        y_window[3] = (uint8_t)(LCD_Y_OFFSET + FONT_SMALL_CELL_HEIGHT - 1u);
    } else {
        const uint16_t row0 = (uint16_t)(step - 1u) * LCD_GRAPH_CHUNK_HEIGHT;
        const uint16_t gy0 = LCD_GRAPH_TOP + row0;
        const uint8_t rows = (uint8_t)((LCD_HEIGHT - gy0) < LCD_GRAPH_CHUNK_HEIGHT ? LCD_HEIGHT - gy0 : LCD_GRAPH_CHUNK_HEIGHT);
        lcd_text_glyph_pixels = (uint16_t)(LCD_WIDTH * rows);
        for (uint8_t row = 0u; row < rows; ++row) {
            const uint16_t gy = gy0 + row;
            for (uint16_t x = 0u; x < LCD_WIDTH; ++x) {
                lcd_glyph_put((uint16_t)row * LCD_WIDTH + x,
                    lcd_graph_pixel(x, gy));
            }
        }
        y_window[1] = (uint8_t)(LCD_Y_OFFSET + gy0);
        y_window[3] = (uint8_t)(y_window[1] + rows - 1u);
    }
    y_window[0] = 0u; y_window[2] = 0u;
    lcd_submit_text_window(x_window, y_window);
    return true;
}

static __attribute__((noinline)) void lcd_render_small_line(
    const char line[20], uint16_t row_width, uint16_t base_color,
    uint8_t selected_start, uint8_t selected_end,
    uint8_t special_start, uint8_t special_end, uint16_t special_color)
{
    lcd_text_glyph_pixels = (uint16_t)(row_width * FONT_SMALL_CELL_HEIGHT);
    lcd_text_dma_glyph_advance = 1u;
    for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
        uint8_t column = 0u;
        for (uint16_t x_start = 0u; x_start < row_width;
             x_start = (uint16_t)(x_start + 8u), ++column) {
            uint16_t color = base_color;
            if (column >= selected_start && column <= selected_end) {
                color = LCD_SETTINGS_SELECTED_COLOR;
            }
            if (column >= special_start && column <= special_end) {
                color = special_color;
            }
            const uint16_t remaining = (uint16_t)(row_width - x_start);
            const uint8_t pixels = (uint8_t)(remaining < 8u ? remaining : 8u);
            for (uint8_t glyph_x = 0u; glyph_x < pixels; ++glyph_x) {
                const bool lit = glyph_x < FONT_SMALL_CELL_WIDTH &&
                    font_small_pixel(line[column], glyph_x, y);
                lcd_glyph_put((uint16_t)y * row_width + x_start + glyph_x,
                    lit ? color : LCD_TEXT_BACKGROUND);
            }
        }
    }
}

static __attribute__((noinline)) bool lcd_text_prepare_glyph(void)
{
    lcd_text_palette_count = 0u;
    if (lcd_screen_page == LCD_GRAPH_PAGE) {
        return lcd_graph_prepare_glyph();
    }

    if (lcd_screen_page >= 2u) {
        if (lcd_screen_page == 2u) {
            char line[20];
            uint8_t x_window[4] = { 0u, 0u, 0u, 158u };
            uint8_t y_window[4];
            uint16_t text_color = LCD_TEXT_FOREGROUND;
            uint8_t selected_start = UINT8_MAX;
            uint8_t selected_end = 0u;

            if (lcd_text_glyph_index >= LCD_SETTINGS_VISIBLE_ROWS) {
                lcd_text_active = false;
                return false;
            }
            if (!LCD_CLEANUP_MENU_ACTIVE && lcd_settings_list_active &&
                lcd_settings_editing &&
                (lcd_settings_value_count(lcd_settings_selected) > 3u ||
                 lcd_settings_selected == SETTING_USE_SPI)) {
                lcd_settings_choice_line(lcd_text_glyph_index, line,
                    &selected_start, &selected_end);
                goto settings_line_draw;
            }
            (void)lcd_settings_make_line(lcd_text_glyph_index, line);
            {
                const uint8_t item = lcd_settings_visible_to_item(
                    (uint8_t)(lcd_settings_first_visible_item() +
                        lcd_text_glyph_index));
                const bool selected_row = lcd_settings_list_active &&
                    (LCD_CLEANUP_MENU_ACTIVE ? item == lcd_cleanup_selected :
                     item == lcd_settings_selected);
                const bool action_item = !LCD_CLEANUP_MENU_ACTIVE &&
                    (item == LCD_SETTINGS_POWER_OFF_ITEM ||
                     item == LCD_SETTINGS_REBOOT_ITEM);
                const bool warning_item = LCD_CLEANUP_MENU_ACTIVE ||
                    item == 13u || item == 14u;
                const char *row_label = LCD_CLEANUP_MENU_ACTIVE ?
                    lcd_cleanup_labels[item] : lcd_settings_labels[item];
                uint8_t label_length = 0u;
                while (row_label[label_length] != '\0' &&
                       label_length < 13u) ++label_length;
                text_color = LCD_CLEANUP_MENU_ACTIVE &&
                    selected_row && lcd_cleanup_status == 1u ?
                    LCD_TEXT_GREEN : (LCD_CLEANUP_MENU_ACTIVE &&
                    selected_row && lcd_cleanup_status == 3u ?
                    LCD_TEXT_YELLOW : (warning_item ?
                    (selected_row ? LCD_SETTINGS_SELECTED_COLOR :
                     LCD_TEXT_RED) :
                    (action_item ?
                        (selected_row ? LCD_SETTINGS_SELECTED_COLOR :
                         LCD_TEXT_YELLOW) :
                        (selected_row ? LCD_SETTINGS_SELECTED_COLOR :
                         LCD_TEXT_FOREGROUND))));
                if (selected_row) {
                    selected_start = 0u;
                    selected_end = label_length;
                }
            }
settings_line_draw:
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + 3u +
                lcd_text_glyph_index * 12u);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(y_window[1] +
                FONT_SMALL_CELL_HEIGHT - 1u);
            lcd_render_small_line(line, 159u, text_color,
                selected_start, selected_end, UINT8_MAX, 0u,
                LCD_TEXT_FOREGROUND);
            lcd_submit_text_window(x_window, y_window);
            return true;
        }
        if (lcd_screen_page == 3u) {
            char text[24];
            if (lcd_text_glyph_index >= 6u) {
                lcd_text_active = false;
                return false;
            }
            const uint8_t length = lcd_info_make_line(
                (uint8_t)(lcd_text_glyph_index + 1u), text);
            const uint16_t row_width = (uint16_t)(length *
                (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP) -
                LCD_SMALL_TEXT_GAP);
            const uint8_t x_start = (uint8_t)((LCD_WIDTH - row_width) / 2u);
            const uint8_t y_start = (uint8_t)(2u +
                lcd_text_glyph_index * (FONT_SMALL_CELL_HEIGHT + 2u));
            uint8_t x_window[4] = {
                0u, x_start, 0u, (uint8_t)(x_start + row_width - 1u)
            };
            uint8_t y_window[4] = {
                0u, (uint8_t)(LCD_Y_OFFSET + y_start), 0u,
                (uint8_t)(LCD_Y_OFFSET + y_start + FONT_SMALL_CELL_HEIGHT - 1u)
            };

            const bool spi_error = lcd_debug_subpage == 0u &&
                lcd_text_glyph_index == 4u && text[17] == 'E' &&
                text[18] == 'R' && text[19] == 'R';
            lcd_render_small_line(text, row_width, LCD_TEXT_FOREGROUND,
                UINT8_MAX, 0u, spi_error ? 17u : UINT8_MAX, 19u,
                LCD_TEXT_RED);
            lcd_submit_text_window(x_window, y_window);
            return true;
        }
        lcd_text_active = false;
        return false;
    }

    {
        const uint8_t rate_label_glyph = (uint8_t)(LCD_RATE_MAX_GLYPHS + 4u);
        const uint8_t cpm_glyph = (uint8_t)(LCD_RATE_MAX_GLYPHS + 5u);
        const bool warmup_active = lcd_screen_page == 0u &&
            lcd_settings_value_index[SETTING_HV] == 0u && !lcd_rate_window_ready;
        const bool show_cpm_row = lcd_screen_page == 0u &&
            lcd_settings_value_index[SETTING_RATE_ROW_2] != 2u &&
            lcd_settings_value_index[SETTING_HV] == 0u;
        const uint8_t dose_row_choice = lcd_dose_row_option(
            lcd_settings_value_index[SETTING_DOSE_ROW_2]);
        const bool show_dose_uptime_row = lcd_screen_page == 1u &&
            dose_row_choice == LCD_DOSE_ROW_TIME && !lcd_dose_row_hidden;
        const bool show_dose_second_row = lcd_screen_page == 1u &&
            !lcd_dose_row_hidden &&
            dose_row_choice != LCD_DOSE_ROW_OFF &&
            dose_row_choice != LCD_DOSE_ROW_TIME;
        const bool clear_dose_row = lcd_screen_page == 1u &&
            !show_dose_second_row && !show_dose_uptime_row;
        const bool clear_warmup_box = lcd_screen_page == 0u &&
            lcd_warmup_box_visible && !warmup_active && !show_cpm_row;
        const uint8_t last_glyph = (warmup_active || show_cpm_row ||
            clear_warmup_box || show_dose_second_row ||
            show_dose_uptime_row || clear_dose_row) ? cpm_glyph :
                rate_label_glyph;
        uint8_t x_start;
        uint8_t y_start;
        uint8_t x_window[4];
        uint8_t y_window[4];
        char glyph;

        if (lcd_text_main_rows_only && lcd_text_glyph_index == 1u) {
            lcd_text_glyph_index = cpm_glyph;
        }
        if (lcd_text_glyph_index > last_glyph) {
            lcd_text_active = false;
            lcd_text_main_rows_only = false;
            return false;
        }

        if (lcd_text_glyph_index == 0u) {
            lcd_text_glyph_pixels = LCD_TOP_ROW_PIXELS;
            lcd_text_dma_glyph_advance = 1u;
            for (uint16_t i = 0u; i < LCD_TOP_ROW_PIXELS; ++i) {
                lcd_glyph_put(i, LCD_TEXT_BACKGROUND);
            }
            lcd_top_row_draw_uptime();
            lcd_top_row_draw_mode_letter();
            lcd_top_row_draw_flashlight_letter();
            const uint16_t sun_color = lcd_displayed_solar_percent > 79u ?
                LCD_TEXT_GREEN : (lcd_displayed_solar_percent > 49u ?
                LCD_TEXT_YELLOW : LCD_TEXT_FOREGROUND);
            const uint16_t battery_color = lcd_adc_usb_raw > 100u ?
                LCD_TEXT_GREEN : LCD_TEXT_FOREGROUND;
            lcd_top_row_draw_percent(7u, FONT_SMALL_GLYPH_SUN, 106u,
                                     sun_color);
            lcd_top_row_draw_percent(13u, FONT_SMALL_GLYPH_BATTERY, 154u,
                                     battery_color);

            x_window[0] = 0u;
            x_window[1] = 0u;
            x_window[2] = 0u;
            x_window[3] = (uint8_t)(LCD_WIDTH - 1u);
            y_start = 1u;
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + y_start);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(LCD_Y_OFFSET + y_start +
                FONT_SMALL_CELL_HEIGHT - 1u);
            lcd_submit_text_window(x_window, y_window);
            return true;
        }

        if (lcd_text_glyph_index == 1u || lcd_text_glyph_index == 2u) {
            /* These slots used to draw raw ADC values and clear the full
             * large-number field. Neither operation is needed anymore. */
            ++lcd_text_glyph_index;
            return false;
        }

        if (lcd_screen_page == 0u && lcd_settings_value_index[SETTING_HV] == 1u &&
            lcd_text_glyph_index == 3u) {
            static const char disabled_text[] = "DISABLED";
            const uint8_t text_length = sizeof(disabled_text) - 1u;
            const uint16_t text_width = (uint16_t)(text_length *
                FONT_SMALL_CELL_WIDTH + (text_length - 1u) *
                LCD_SMALL_TEXT_GAP);
            const uint8_t text_x = (uint8_t)((LCD_WIDTH - text_width) / 2u);
            const uint8_t text_y = (uint8_t)((LCD_HEIGHT -
                FONT_SMALL_CELL_HEIGHT) / 2u);

            x_window[0] = 0u;
            x_window[1] = text_x;
            x_window[2] = 0u;
            x_window[3] = (uint8_t)(text_x + text_width - 1u);
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + text_y);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(y_window[1] +
                FONT_SMALL_CELL_HEIGHT - 1u);
            lcd_render_small_line(disabled_text, text_width, LCD_COUNTER_RED,
                UINT8_MAX, 0u, UINT8_MAX, 0u, LCD_TEXT_FOREGROUND);
            lcd_text_dma_glyph_advance =
                (uint8_t)(rate_label_glyph - lcd_text_glyph_index);
            lcd_submit_text_window(x_window, y_window);
            return true;
        }

        if (lcd_text_glyph_index == rate_label_glyph) {
            const char *label = lcd_rate_label;
            char dose_label[LCD_DOSE_LABEL_LENGTH + 1u] = {
                ' ', ' ', ' ', ' ', ' ', '\0'
            };
            uint8_t label_length = LCD_RATE_LABEL_LENGTH;
            const uint16_t label_width = (uint16_t)(label_length *
                FONT_SMALL_CELL_WIDTH + (label_length - 1u) *
                LCD_SMALL_TEXT_GAP);

            if (lcd_screen_page != 0u) {
                if (lcd_settings_value_index[SETTING_UNIT] == 1u) {
                    dose_label[0] = 'C';
                    dose_label[1] = 'N';
                    dose_label[2] = 'T';
                } else if (lcd_settings_value_index[SETTING_UNIT] == 2u) {
                    dose_label[0] = lcd_display_unit_prefix;
                    dose_label[1] = 'R';
                } else {
                    dose_label[0] = lcd_display_unit_prefix;
                    dose_label[1] = 'S';
                    dose_label[2] = 'v';
                }
                /* Two trailing space glyphs overwrite the old /h pixels. */
                label = dose_label;
            }

            {
                const uint8_t value_right_edge = (uint8_t)(LCD_RATE_FIELD_X +
                    LCD_RATE_FIELD_WIDTH - 1u);
                x_start = (uint8_t)(value_right_edge - label_width + 1u);
            }
            /* Keep the row inside the active 80-line panel after the stock
             * controller's vertical offset. */
            y_start = (uint8_t)(LCD_HEIGHT - FONT_SMALL_CELL_HEIGHT - 7u);
            x_window[0] = 0u;
            x_window[1] = x_start;
            x_window[2] = 0u;
            x_window[3] = (uint8_t)(x_start + label_width - 1u);
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + y_start);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(LCD_Y_OFFSET + y_start +
                FONT_SMALL_CELL_HEIGHT - 1u);

            lcd_render_small_line(label, label_width,
                lcd_screen_page == 0u ? lcd_rate_color : LCD_TEXT_FOREGROUND,
                UINT8_MAX, 0u, UINT8_MAX, 0u, LCD_TEXT_FOREGROUND);

            lcd_submit_text_window(x_window, y_window);
            return true;
        }

        if (lcd_text_glyph_index == cpm_glyph &&
            (warmup_active || show_cpm_row || show_dose_second_row ||
                show_dose_uptime_row || clear_dose_row ||
                clear_warmup_box)) {
            const uint8_t text_length = warmup_active ?
                LCD_WARMUP_TEXT_LENGTH : LCD_CPM_TEXT_LENGTH;
            const uint16_t text_width = warmup_active ? LCD_CPM_TEXT_WIDTH :
                (uint16_t)(text_length *
                (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP) -
                LCD_SMALL_TEXT_GAP);
            const uint16_t text_x_start = warmup_active ?
                (uint16_t)((LCD_WIDTH - text_width) / 2u -
                    (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP) -
                    (3u * (FONT_SMALL_CELL_WIDTH +
                        LCD_SMALL_TEXT_GAP)) / 2u -
                    (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP)) :
                (uint16_t)(2u * (FONT_SMALL_CELL_WIDTH +
                    LCD_SMALL_TEXT_GAP));
            const uint8_t text_y_start =
            (uint8_t)(LCD_HEIGHT - FONT_SMALL_CELL_HEIGHT - 4u);
            const uint8_t y_start = (uint8_t)(text_y_start - 2u);
            uint8_t x_window[4] = {
                (uint8_t)(text_x_start >> 8u), (uint8_t)text_x_start,
                (uint8_t)((text_x_start + text_width - 1u) >> 8u),
                (uint8_t)(text_x_start + text_width - 1u)
            };
            uint8_t y_window[4] = {
                0u, (uint8_t)(LCD_Y_OFFSET + y_start), 0u,
                (uint8_t)(LCD_Y_OFFSET + y_start +
                          LCD_WARMUP_BOX_HEIGHT - 1u)
            };

            lcd_text_glyph_pixels = (uint16_t)(text_width *
                LCD_WARMUP_BOX_HEIGHT);
            lcd_text_dma_glyph_advance = 1u;
            for (uint16_t i = 0u; i < lcd_text_glyph_pixels; ++i) {
                lcd_glyph_put(i, LCD_TEXT_BACKGROUND);
            }
            {
                char warmup_text[LCD_WARMUP_TEXT_LENGTH + 1u] = {
                    '0', '0', ' ', 'C', 'N', 'T', '\0'
                };
                const uint32_t count = lcd_warmup_pulses > 30u ? 30u :
                    lcd_warmup_pulses;
                if (warmup_active) {
                    const uint16_t box_x = (uint16_t)((text_width -
                        LCD_WARMUP_BOX_WIDTH) / 2u);
                    const uint16_t text_glyph_width = (uint16_t)(
                        LCD_WARMUP_TEXT_LENGTH *
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP) -
                        LCD_SMALL_TEXT_GAP);
                    const uint16_t text_x = (uint16_t)(box_x +
                        (LCD_WARMUP_BOX_WIDTH - text_glyph_width) / 2u);
                    const uint16_t fill_width = (uint16_t)(count *
                        (LCD_WARMUP_BOX_WIDTH - 2u) / 30u);
                    warmup_text[0] = (char)('0' + count / 10u);
                    warmup_text[1] = (char)('0' + count % 10u);
                    for (uint8_t y = 0u; y < LCD_WARMUP_BOX_HEIGHT; ++y) {
                        for (uint16_t x = 0u; x < LCD_WARMUP_BOX_WIDTH; ++x) {
                            const bool frame = y == 0u ||
                                y == LCD_WARMUP_BOX_HEIGHT - 1u ||
                                x == 0u || x == LCD_WARMUP_BOX_WIDTH - 1u;
                            const bool filled = !frame && x > 0u &&
                                x <= fill_width;
                            lcd_glyph_put((uint16_t)y * text_width +
                                box_x + x, frame ? LCD_COLOR_PROGRESS_FRAME :
                                (filled ? LCD_COLOR_PROGRESS_FILL :
                                    LCD_TEXT_BACKGROUND));
                        }
                    }
                    for (uint8_t column = 0u;
                         column < LCD_WARMUP_TEXT_LENGTH; ++column) {
                        for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
                            for (uint8_t x = 0u; x < FONT_SMALL_CELL_WIDTH; ++x) {
                                if (font_small_pixel(warmup_text[column], x, y)) {
                                    const uint16_t pixel =
                                        (uint16_t)(y + 1u) * text_width + text_x +
                                        column * (FONT_SMALL_CELL_WIDTH +
                                            LCD_SMALL_TEXT_GAP) + x;
                                    lcd_glyph_put(pixel, LCD_TEXT_FOREGROUND);
                                }
                            }
                        }
                    }
                    lcd_warmup_box_visible = true;
                } else if (show_cpm_row || show_dose_second_row ||
                           show_dose_uptime_row) {
                    if (show_dose_second_row) lcd_format_dose_row();
                    if (show_dose_uptime_row) lcd_format_dose_uptime_row();
                    for (uint8_t column = 0u;
                         column < LCD_CPM_TEXT_LENGTH; ++column) {
                        lcd_top_row_draw_glyph(lcd_cpm_text[column],
                            (uint16_t)(column * (FONT_SMALL_CELL_WIDTH +
                                LCD_SMALL_TEXT_GAP)), text_width,
                            LCD_COUNTER_LIGHT_GRAY);
                    }
                    lcd_warmup_box_visible = false;
                } else if (clear_dose_row) {
                    for (uint8_t i = 0u; i < LCD_CPM_TEXT_LENGTH; ++i) {
                        lcd_cpm_text[i] = ' ';
                    }
                    lcd_warmup_box_visible = false;
                } else {
                    /* RATE ROW 2 is OFF: blank the old progress box. */
                    lcd_warmup_box_visible = false;
                }
            }

            lcd_submit_text_window(x_window, y_window);
            return true;
        }

        if (lcd_text_glyph_index == cpm_glyph &&
            !warmup_active && !show_cpm_row) {
            if (!show_dose_second_row && !show_dose_uptime_row &&
                !clear_dose_row) {
                ++lcd_text_glyph_index;
                return false;
            }
        }

        {
            const uint8_t digit_slot = (uint8_t)(lcd_text_glyph_index - 3u);
            const bool extended_value =
                lcd_rate_glyph_count > LCD_RATE_MASK_GLYPHS;
            const uint8_t digit_index = digit_slot;
            const uint8_t first_digit = extended_value ? 0u :
                (uint8_t)((LCD_RATE_MASK_GLYPHS -
                    lcd_rate_glyph_count + 1u) / 2u);
            const uint8_t content_end = (uint8_t)(first_digit +
                lcd_rate_glyph_count);
            uint16_t glyph_x;
            uint16_t text_width = 0u;
            uint8_t glyph_width;

            glyph = digit_index < first_digit ||
                (!extended_value && digit_index >= content_end) ? ' ' :
                lcd_rate_glyphs[digit_index - first_digit];
            if (extended_value) {
                for (uint8_t i = 0u; i < lcd_rate_glyph_count; ++i) {
                    text_width = (uint16_t)(text_width +
                        font_large_glyph_width(lcd_rate_glyphs[i]));
                }
                glyph_x = (uint16_t)((LCD_WIDTH - text_width) / 2u);
                for (uint8_t i = 0u; i < digit_index; ++i) {
                    glyph_x = (uint16_t)(glyph_x +
                        font_large_glyph_width(lcd_rate_glyphs[i]));
                }
            } else {
                glyph_x = (uint16_t)(LCD_RATE_FIELD_X +
                    digit_index * FONT_LARGE_CELL_WIDTH);
            }
            x_start = (uint8_t)glyph_x;
            glyph_width = (digit_index < first_digit ||
                (!extended_value && digit_index >= content_end)) ?
                FONT_LARGE_CELL_WIDTH :
                font_large_glyph_width(glyph);

            y_start = (uint8_t)((LCD_HEIGHT - FONT_LARGE_CELL_HEIGHT) / 2u);
            x_window[0] = 0u;
            x_window[1] = x_start;
            x_window[2] = 0u;
            x_window[3] = (uint8_t)(x_start + glyph_width - 1u);
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + y_start);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(LCD_Y_OFFSET + y_start +
                FONT_LARGE_CELL_HEIGHT - 1u);

            lcd_text_glyph_pixels = (uint16_t)(glyph_width *
                FONT_LARGE_CELL_HEIGHT);
            lcd_text_dma_glyph_advance = 1u;
            for (uint8_t y = 0u; y < FONT_LARGE_CELL_HEIGHT; ++y) {
                for (uint8_t x = 0u; x < glyph_width; ++x) {
                    lcd_glyph_put((uint16_t)y * glyph_width + x, font_large_pixel(glyph, x, y) ?
                            (lcd_screen_page == 0u ? lcd_rate_color :
                             LCD_TEXT_FOREGROUND) : LCD_TEXT_BACKGROUND);
                }
            }

            lcd_submit_text_window(x_window, y_window);
            return true;
        }
    }

}

static void lcd_indication_mode_next(void)
{
    if (lcd_indication_mode == 2u &&
        (uint32_t)(board_millis - lcd_search_mode_started) >= 10000u) {
        lcd_indication_mode = 1u;
    } else {
        lcd_indication_mode = (uint8_t)((lcd_indication_mode + 1u) % 3u);
        if (lcd_indication_mode == 2u) lcd_search_mode_started = board_millis;
    }
}

void board_display_left_short(void)
{
    if (lcd_screen_page == LCD_GRAPH_PAGE) {
        if (lcd_graph_input_locked) {
            lcd_graph_pending_action = LCD_GRAPH_PENDING_LEFT;
            return;
        }
        if (lcd_graph_cursor_mode) {
            (void)lcd_graph_move_bar(false);
            return;
        }
        lcd_graph_select_period(false);
        return;
    }
    if (lcd_screen_page >= 2u) {
        board_display_extra_step(false);
        return;
    }
    lcd_indication_mode_next();
    if (lcd_screen_page == 1u) lcd_dose_row_hidden = false;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
}

void board_display_right_short(void)
{
    if (lcd_screen_page == LCD_GRAPH_PAGE) {
        if (lcd_graph_input_locked) {
            lcd_graph_pending_action = LCD_GRAPH_PENDING_RIGHT;
            return;
        }
        if (lcd_graph_cursor_mode) {
            (void)lcd_graph_move_bar(true);
            return;
        }
        lcd_graph_select_period(true);
        return;
    }
    if (lcd_screen_page >= 2u) {
        board_display_extra_step(true);
        return;
    }
    lcd_screen_page = (uint8_t)((lcd_screen_page + 1u) % 2u);
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    if (lcd_screen_page == 0u) {
        lcd_format_rate_display(lcd_rate_value_thousandths);
        lcd_format_cpm(lcd_current_cpm);
    } else {
        lcd_format_dose_display(lcd_dose_value_thousandths,
                                lcd_dose_fraction);
    }
    lcd_cpm_redraw_pending = true;
}

bool board_display_search_mode(void)
{
    return lcd_indication_mode == 2u;
}

bool board_display_invisible_mode(void)
{
    return lcd_indication_mode == 0u && lcd_screen_page < 2u;
}

bool board_display_use_spi(void)
{
    return settings_spi_enabled;
}

uint8_t board_display_log_memory(void)
{
    return settings_log_memory;
}

uint8_t board_display_log_source(void)
{
    return settings_log_source;
}

uint8_t board_display_dose_period(void)
{
    return lcd_settings_value_index[SETTING_DOSE_PERIOD];
}

uint16_t board_display_battery_start_raw(void)
{
    return lcd_adc_battery_start_raw;
}

bool board_display_usb_connected(void)
{
    return lcd_adc_usb_raw > 100u;
}

bool board_display_rate_long_press(void)
{
    if (lcd_screen_page > 1u || lcd_settings_list_active) return false;

    flashlight_manual_enabled = !flashlight_manual_enabled;
    flashlight_enabled = flashlight_alarm_override ? flashlight_alarm_output :
        flashlight_manual_enabled;
    REG32(GPIOB_BASE + GPIO_BSRR) = flashlight_enabled ?
        (1u << FLASHLIGHT_PIN) : (1u << (16u + FLASHLIGHT_PIN));
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_cpm_redraw_pending = true;
    return true;
}

bool board_display_graph_enter(void)
{
    if (lcd_screen_page != 1u || lcd_settings_list_active) return false;
    lcd_main_return_page = 1u;
    lcd_screen_page = LCD_GRAPH_PAGE;
    lcd_graph_cursor_mode = false;
    lcd_graph_partial_pending = false;
    lcd_graph_partial_kind = 0u;
    if (lcd_graph_log_type != 0u) {
        /* Re-enter through the same WAIT frame used when changing graph type.
         * Cached log windows made the next header render rebuild/refresh an
         * entire log synchronously, which could stall the UI after returning
         * from Dose. */
        lcd_graph_reset_log_window();
        lcd_graph_loading = true;
    }
    lcd_graph_log_summary_work_active = false;
    lcd_graph_refresh_deadline = board_millis + (lcd_graph_log_type ? 5000u : 500u);
    lcd_graph_input_locked = true;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_dma_text_glyph_advance = 0u;
    lcd_background_pending = false;
    lcd_cpm_redraw_pending = true;
    return true;
}

bool board_display_graph_long_press(void)
{
    if (lcd_screen_page != LCD_GRAPH_PAGE) return false;
    if (lcd_graph_input_locked) { lcd_graph_pending_action = LCD_GRAPH_PENDING_TOGGLE; return true; }
    if (lcd_graph_log_type) {
        lcd_graph_previous_selected = lcd_graph_selected;
        lcd_graph_cursor_mode = !lcd_graph_cursor_mode;
        if (lcd_graph_cursor_mode) {
            lcd_graph_selected_record = 0u;
            lcd_graph_selected = 0u;
        }
        lcd_graph_partial_kind = LCD_GRAPH_PARTIAL_CURSOR;
        lcd_graph_partial_pending = true;
    } else {
        lcd_graph_range_index = (uint8_t)((lcd_graph_range_index + 1u) % 4u);
        lcd_cpm_redraw_pending = true;
    }
    lcd_graph_input_locked = true;
    lcd_graph_refresh_deadline = board_millis + (lcd_graph_log_type ? 5000u : 500u);
    if (!lcd_graph_log_type) {
        lcd_text_active = false;
        lcd_text_row_commands_queued = false;
    }
    return true;
}

bool board_display_graph_back(void)
{
    if (lcd_screen_page != LCD_GRAPH_PAGE || !lcd_graph_cursor_mode) return false;
    lcd_graph_previous_selected = lcd_graph_selected;
    lcd_graph_cursor_mode = false;
    lcd_graph_input_locked = true;
    lcd_graph_refresh_deadline = board_millis + 5000u;
    lcd_graph_partial_kind = LCD_GRAPH_PARTIAL_CURSOR;
    lcd_graph_partial_pending = true;
    return true;
}

void board_flashlight_alarm_set(bool active, bool on)
{
    const bool desired = active ? on : flashlight_manual_enabled;

    flashlight_alarm_override = active;
    flashlight_alarm_output = on;
    if (flashlight_enabled == desired) return;
    flashlight_enabled = desired;
    REG32(GPIOB_BASE + GPIO_BSRR) = flashlight_enabled ?
        (1u << FLASHLIGHT_PIN) : (1u << (16u + FLASHLIGHT_PIN));
    if (lcd_screen_page < 2u) {
        lcd_text_active = false;
        lcd_text_row_commands_queued = false;
        lcd_cpm_redraw_pending = true;
    }
}

static void lcd_debug_probe_spi(void);

void board_display_extra_enter(void)
{
    if (lcd_screen_page >= 2u) {
        return;
    }
    lcd_main_return_page = lcd_screen_page;
    lcd_screen_page = lcd_last_extra_page;
    if (lcd_screen_page == 3u) lcd_debug_probe_spi();
    lcd_settings_list_active = false;
    lcd_settings_editing = false;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
}

bool board_display_extra_active(void)
{
    return lcd_screen_page >= 2u && lcd_screen_page < LCD_GRAPH_PAGE;
}

void board_display_extra_step(bool forward)
{
    if (lcd_screen_page < 2u) {
        return;
    }
    if (forward) {
        lcd_screen_page = (uint8_t)(2u +
            ((lcd_screen_page - 2u + 1u) % LCD_EXTRA_SCREEN_COUNT));
    } else {
        lcd_screen_page = (uint8_t)(2u +
            ((lcd_screen_page - 2u + LCD_EXTRA_SCREEN_COUNT - 1u) %
             LCD_EXTRA_SCREEN_COUNT));
    }
    lcd_last_extra_page = lcd_screen_page;
    if (lcd_screen_page == 3u) lcd_debug_probe_spi();
    lcd_settings_list_active = false;
    lcd_settings_editing = false;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
}

void board_display_extra_exit(void)
{
    if (lcd_screen_page < 2u) {
        return;
    }
    if (lcd_screen_page < LCD_GRAPH_PAGE) {
        lcd_last_extra_page = lcd_screen_page;
    }
    lcd_screen_page = lcd_main_return_page;
    lcd_graph_input_locked = false;
    lcd_graph_pending_action = 0;
    lcd_graph_log_summary_work_active = false;
    lcd_graph_loading = false;
    lcd_settings_list_active = false;
    lcd_settings_editing = false;
    lcd_graph_column_y[0] = UINT8_MAX;
    if (lcd_screen_page == 0u) {
        lcd_format_rate_display(lcd_rate_value_thousandths);
        lcd_format_cpm(lcd_current_cpm);
    } else {
        lcd_format_dose_display(lcd_dose_value_thousandths,
                                lcd_dose_fraction);
    }
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
}

void board_display_force_home(void)
{
    /* Keep the active LCD/DMA transaction intact, but stop queuing graph or
     * menu work and repaint the rate screen as soon as that chunk completes. */
    lcd_screen_page = 0u;
    lcd_main_return_page = 0u;
    lcd_graph_input_locked = false;
    lcd_graph_pending_action = 0;
    lcd_graph_log_summary_work_active = false;
    lcd_graph_loading = false;
    lcd_settings_list_active = false;
    lcd_settings_editing = false;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_dma_text_glyph_advance = 0u;
    lcd_cpm_redraw_pending = true;
    lcd_background_pending = true;
}

static void board_display_settings_redraw(void);

static void lcd_debug_probe_spi(void)
{
    if (spi_nor_is_ready() || spi_nor_size_bytes() != 0u) return;
    (void)spi_nor_init();
    if (board_display_log_memory() != BOARD_LOG_MEMORY_SPI) {
        spi_nor_deinit();
    }
}

#define SETTINGS_EEPROM_LEGACY_SLOT_SIZE 5u
#define SETTINGS_EEPROM_V2_BASE 16u
#define SETTINGS_EEPROM_V2_SLOT_SIZE 7u
#define SETTINGS_EEPROM_V3_BASE 32u
#define SETTINGS_EEPROM_V3_SLOT_SIZE 9u
#define SETTINGS_TIMER_EEPROM_BASE 64u
#define SETTINGS_TIMER_EEPROM_SLOT_SIZE 4u
#define SETTINGS_ALARM_EEPROM_BASE 80u
#define SETTINGS_ALARM_EEPROM_SLOT_SIZE 5u
#define SETTINGS_HV_EEPROM_BASE 96u
#define SETTINGS_HV_EEPROM_SLOT_SIZE 4u
#define SETTINGS_LIGHT_ALARM_EEPROM_BASE 104u
#define SETTINGS_LIGHT_ALARM_EEPROM_SLOT_SIZE 4u
#define SETTINGS_DOSE_ALARM_EEPROM_BASE 112u
#define SETTINGS_DOSE_ALARM_EEPROM_SLOT_SIZE 4u
#define SETTINGS_ROW_EEPROM_BASE 50u
#define SETTINGS_ROW_EEPROM_SLOT_SIZE 6u
#define SETTINGS_ROW_EEPROM_MAGIC 0xC8u
#define SETTINGS_ROW_EEPROM_LEGACY_MAGIC 0xBDu
#define SETTINGS_EEPROM_MAGIC 0xA7u
#define SETTINGS_EEPROM_V2_MAGIC 0xA8u
#define SETTINGS_EEPROM_V3_MAGIC 0xA9u
#define SETTINGS_EEPROM_V4_MAGIC 0xAAu
#define SETTINGS_TIMER_EEPROM_MAGIC 0xB1u
#define SETTINGS_ALARM_EEPROM_MAGIC 0xB2u
#define SETTINGS_ALARM_V2_EEPROM_MAGIC 0xB7u
#define SETTINGS_HV_EEPROM_MAGIC 0xB3u
#define SETTINGS_LIGHT_ALARM_EEPROM_MAGIC 0xB4u
#define SETTINGS_DOSE_ALARM_EEPROM_MAGIC 0xB5u

static uint8_t settings_record_crc(uint8_t sequence, uint8_t lead_zero,
                                   uint8_t unit, uint8_t rate_prefix,
                                   uint8_t dose_prefix)
{
    return (uint8_t)(0x5Du ^ sequence ^ lead_zero ^ unit ^
        (rate_prefix * 17u) ^ (dose_prefix * 31u));
}

static bool settings_v2_record_read(uint8_t slot, uint8_t *sequence,
                                    uint8_t *lead_zero, uint8_t *unit,
                                    uint8_t *rate_prefix, uint8_t *dose_prefix)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_EEPROM_V2_BASE +
        slot * SETTINGS_EEPROM_V2_SLOT_SIZE);
    const uint8_t magic = record[0];
    *sequence = record[1];
    *lead_zero = record[2];
    *unit = record[3];
    *rate_prefix = record[4];
    *dose_prefix = record[5];
    return magic == SETTINGS_EEPROM_V2_MAGIC && *lead_zero < 2u &&
        *unit < 3u && *rate_prefix < (*unit == 1u ? 2u : 4u) &&
        *dose_prefix < (*unit == 1u ? 2u : 4u) &&
        record[6] == settings_record_crc(*sequence, *lead_zero, *unit,
            *rate_prefix, *dose_prefix);
}

static uint8_t settings_v3_crc(uint8_t sequence, uint8_t lead_zero,
                               uint8_t unit, uint8_t rate_prefix,
                               uint8_t dose_prefix, uint8_t led_brightness,
                               uint8_t screen_brightness)
{
    return (uint8_t)(settings_record_crc(sequence, lead_zero, unit,
        rate_prefix, dose_prefix) ^ (led_brightness * 13u) ^
        (screen_brightness * 23u));
}

static uint8_t settings_row_crc(uint8_t sequence, uint8_t rate_row,
                                uint8_t dose_row, uint8_t dose_period)
{
    return (uint8_t)(0x6Bu ^ (sequence * 3u) ^ (rate_row * 7u) ^
        (dose_row * 11u) ^ (dose_period * 17u));
}

static bool settings_row_record_read(uint8_t slot, uint8_t *sequence,
                                     uint8_t *rate_row, uint8_t *dose_row,
                                     uint8_t *dose_period, bool *legacy)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_ROW_EEPROM_BASE +
        slot * SETTINGS_ROW_EEPROM_SLOT_SIZE);
    *sequence = record[1];
    *rate_row = record[2];
    *dose_row = record[3];
    *dose_period = record[4];
    const bool old_format = record[0] == SETTINGS_ROW_EEPROM_LEGACY_MAGIC;
    if (legacy != NULL) *legacy = old_format;
    return (record[0] == SETTINGS_ROW_EEPROM_MAGIC && *rate_row < 4u &&
            *dose_row < 7u && *dose_period < 3u &&
            record[5] == settings_row_crc(*sequence, *rate_row, *dose_row,
                *dose_period)) ||
        (old_format && *rate_row < 9u && *dose_row < 4u &&
            *dose_period < 3u && record[5] == settings_row_crc(*sequence,
                *rate_row, *dose_row, *dose_period));
}

static bool settings_eeprom_write_byte(uint16_t offset, uint8_t value);
static bool settings_row_save(void);

static void settings_row_load(void)
{
    uint8_t seq0, rate0, dose0, period0;
    uint8_t seq1, rate1, dose1, period1;
    bool legacy0, legacy1;
    const bool valid0 = settings_row_record_read(0u, &seq0, &rate0,
        &dose0, &period0, &legacy0);
    const bool valid1 = settings_row_record_read(1u, &seq1, &rate1,
        &dose1, &period1, &legacy1);
    if (!valid0 && !valid1) return;
    const bool use1 = valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0);
    lcd_row_settings_sequence = use1 ? seq1 : seq0;
    const bool legacy = use1 ? legacy1 : legacy0;
    lcd_settings_value_index[SETTING_RATE_ROW_2] = use1 ? rate1 : rate0;
    lcd_settings_value_index[SETTING_DOSE_ROW_2] = use1 ? dose1 : dose0;
    lcd_settings_value_index[SETTING_DOSE_PERIOD] = use1 ? period1 : period0;
    if (legacy && lcd_settings_value_index[SETTING_RATE_ROW_2] >= 3u)
        lcd_settings_value_index[SETTING_RATE_ROW_2] = 0u;
    if (lcd_settings_value_index[SETTING_RATE_ROW_2] >=
            lcd_settings_value_count(SETTING_RATE_ROW_2)) {
        lcd_settings_value_index[SETTING_RATE_ROW_2] = 0u;
    }
    if (lcd_settings_value_index[SETTING_DOSE_ROW_2] >=
            lcd_settings_value_count(SETTING_DOSE_ROW_2)) {
        lcd_settings_value_index[SETTING_DOSE_ROW_2] = 0u;
    }
    if (legacy) (void)settings_row_save();
}

static bool settings_row_save(void)
{
    uint8_t seq0, rate0, dose0, period0;
    uint8_t seq1, rate1, dose1, period1;
    const bool valid0 = settings_row_record_read(0u, &seq0, &rate0,
        &dose0, &period0, NULL);
    const bool valid1 = settings_row_record_read(1u, &seq1, &rate1,
        &dose1, &period1, NULL);
    const uint8_t current = valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0) ?
        1u : 0u;
    const uint8_t target = (valid0 || valid1) ? (uint8_t)(current ^ 1u) : 0u;
    const uint8_t sequence = (uint8_t)(lcd_row_settings_sequence + 1u);
    const uint8_t base = (uint8_t)(SETTINGS_ROW_EEPROM_BASE +
        target * SETTINGS_ROW_EEPROM_SLOT_SIZE);
    const uint8_t rate_row = lcd_settings_value_index[SETTING_RATE_ROW_2];
    const uint8_t dose_row = lcd_settings_value_index[SETTING_DOSE_ROW_2];
    const uint8_t period = lcd_settings_value_index[SETTING_DOSE_PERIOD];
    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), rate_row) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u), dose_row) ||
        !settings_eeprom_write_byte((uint8_t)(base + 4u), period) ||
        !settings_eeprom_write_byte((uint8_t)(base + 5u),
            settings_row_crc(sequence, rate_row, dose_row, period)) ||
        !settings_eeprom_write_byte(base, SETTINGS_ROW_EEPROM_MAGIC)) {
        return false;
    }
    lcd_row_settings_sequence = sequence;
    return true;
}

static bool settings_v3_record_read(uint8_t slot, uint8_t *sequence,
                                    uint8_t *lead_zero, uint8_t *unit,
                                    uint8_t *rate_prefix, uint8_t *dose_prefix,
                                    uint8_t *led_brightness,
                                    uint8_t *screen_brightness,
                                    uint8_t *led_time)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_EEPROM_V3_BASE +
        slot * SETTINGS_EEPROM_V3_SLOT_SIZE);
    *sequence = record[1];
    *lead_zero = record[2];
    *unit = record[3];
    *rate_prefix = record[4];
    *dose_prefix = record[5];
    const uint8_t magic = record[0];
    const uint8_t led_record = record[6];
    *led_brightness = magic == SETTINGS_EEPROM_V4_MAGIC ?
        (uint8_t)(led_record & 0x0Fu) : led_record;
    *led_time = magic == SETTINGS_EEPROM_V4_MAGIC ?
        (uint8_t)(led_record >> 4) : 0u;
    *screen_brightness = record[7];
    return (magic == SETTINGS_EEPROM_V3_MAGIC ||
            magic == SETTINGS_EEPROM_V4_MAGIC) && *lead_zero < 2u &&
        *unit < 3u && *rate_prefix < (*unit == 1u ? 2u : 4u) &&
        *dose_prefix < (*unit == 1u ? 2u : 4u) &&
        *led_brightness < 9u && *screen_brightness < 9u && *led_time < 9u &&
        record[8] == settings_v3_crc(*sequence, *lead_zero, *unit,
            *rate_prefix, *dose_prefix, magic == SETTINGS_EEPROM_V4_MAGIC ?
                led_record : *led_brightness, *screen_brightness);
}

static bool settings_legacy_record_read(uint8_t slot, uint8_t *sequence,
                                        uint8_t *lead_zero, uint8_t *unit)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + slot * SETTINGS_EEPROM_LEGACY_SLOT_SIZE);
    *sequence = record[1];
    *lead_zero = record[2];
    *unit = record[3];
    return record[0] == SETTINGS_EEPROM_MAGIC && *lead_zero < 2u &&
        *unit < 3u && record[4] == (uint8_t)(0x5Du ^ *sequence ^
            *lead_zero ^ *unit);
}

static bool settings_eeprom_write_byte(uint16_t offset, uint8_t value)
{
    if (offset >= 2048u) return false;
    volatile uint8_t *address = (volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + offset);
    uint32_t timeout = 1000000u;

    while ((FLASH_SR & FLASH_SR_BSY) != 0u && --timeout != 0u) { }
    if (timeout == 0u) return false;
    if ((FLASH_PECR & FLASH_PECR_PELOCK) != 0u) {
        FLASH_PEKEYR = 0x89ABCDEFu;
        FLASH_PEKEYR = 0x02030405u;
    }
    if ((FLASH_PECR & FLASH_PECR_PELOCK) != 0u) return false;
    FLASH_PECR = FLASH_PECR_DATA;
    *address = value;
    timeout = 1000000u;
    while ((FLASH_SR & FLASH_SR_BSY) != 0u && --timeout != 0u) { }
    FLASH_PECR |= FLASH_PECR_PELOCK;
    return timeout != 0u && *address == value;
}

bool board_eeprom_read(uint16_t offset, uint8_t *value)
{
    if (offset >= 2048u || value == NULL) return false;
    *value = *(volatile const uint8_t *)(uintptr_t)(DATA_EEPROM_BASE + offset);
    return true;
}

bool board_eeprom_write(uint16_t offset, uint8_t value)
{
    return settings_eeprom_write_byte(offset, value);
}

static bool board_eeprom_clear_all(void)
{
    for (uint16_t address = 0u; address < 2048u; ++address) {
        if (!settings_eeprom_write_byte(address, 0xFFu)) return false;
    }
    return true;
}

#define SETTINGS_CLICKS_EEPROM_BASE 120u
#define SETTINGS_CLICKS_MAGIC 0xB6u
#define SETTINGS_CLICK_TONE_EEPROM_BASE 124u
#define SETTINGS_CLICK_TONE_MAGIC 0xB8u
#define SETTINGS_SOUND_ALARM_EEPROM_BASE 128u
#define SETTINGS_SOUND_ALARM_MAGIC 0xB9u
#define SETTINGS_ALARM_STOP_EEPROM_BASE 132u
#define SETTINGS_ALARM_STOP_MAGIC 0xBAu
#define SETTINGS_SPI_EEPROM_BASE 136u
#define SETTINGS_SPI_EEPROM_MAGIC 0xBCu
#define SETTINGS_LONG_PRESS_EEPROM_BASE 77u
#define SETTINGS_LONG_PRESS_LEGACY_EEPROM_BASE 140u
#define SETTINGS_LONG_PRESS_MAGIC 0xBDu
#define SETTINGS_RATE_CUSTOM_EEPROM_BASE 144u
#define SETTINGS_RATE_CUSTOM_MAGIC 0xC1u
#define SETTINGS_DOSE_SCREEN_EEPROM_BASE 147u
#define SETTINGS_DOSE_LIGHT_EEPROM_BASE 150u
#define SETTINGS_DOSE_SOUND_EEPROM_BASE 153u
#define SETTINGS_VISIBILITY_EEPROM_BASE 72u
#define SETTINGS_VISIBILITY_MAGIC 0xC7u
#define SETTINGS_VISIBILITY_OLD_MAGIC 0xC6u
static bool settings_visibility_save(uint32_t mask);
static uint32_t settings_visibility_default_mask(void)
{
    return (1UL << SETTING_RATE_CUSTOM) |
        (1UL << SETTING_DOSE_CUSTOM) |
        (1UL << SETTING_LONG_PRESS) |
        (1UL << SETTING_HV) |
        (1UL << SETTING_USE_SPI) |
        (1UL << SETTING_DOSE_SCREEN) |
        (1UL << SETTING_DOSE_LIGHT) |
        (1UL << SETTING_DOSE_SOUND) |
        (1UL << SETTING_ALARM_STOP) |
        (1UL << SETTING_TONE) |
        (1UL << SETTING_LEAD_ZERO) |
        (1UL << SETTING_RATE_PREFIX) |
        (1UL << SETTING_DOSE_PREFIX) |
        (1UL << SETTING_RESET) |
        (1UL << SETTING_REBOOT);
}

static bool settings_value_read(uint16_t offset, uint8_t magic, uint8_t salt,
                                 uint8_t count, uint8_t *value)
{
    uint8_t stored_magic, stored_value, stored_crc;
    if (!board_eeprom_read(offset, &stored_magic) ||
        !board_eeprom_read(offset + 1u, &stored_value) ||
        !board_eeprom_read(offset + 2u, &stored_crc) ||
        stored_magic != magic || stored_value >= count ||
        stored_crc != (uint8_t)(salt ^ stored_value)) return false;
    *value = stored_value;
    return true;
}

static bool settings_value_write(uint16_t offset, uint8_t magic, uint8_t salt,
                                  uint8_t value)
{
    return board_eeprom_write(offset, 0u) &&
        board_eeprom_write(offset + 1u, value) &&
        board_eeprom_write(offset + 2u, (uint8_t)(salt ^ value)) &&
        board_eeprom_write(offset, magic);
}

static void settings_visibility_load(void)
{
    uint8_t magic;
    uint8_t bytes[4];
    if (!board_eeprom_read(SETTINGS_VISIBILITY_EEPROM_BASE, &magic) ||
        (magic != SETTINGS_VISIBILITY_MAGIC &&
         magic != SETTINGS_VISIBILITY_OLD_MAGIC)) {
        lcd_settings_hidden_mask = settings_visibility_default_mask();
        return;
    }
    for (uint8_t i = 0u; i < 4u; ++i) {
        if (!board_eeprom_read((uint16_t)(SETTINGS_VISIBILITY_EEPROM_BASE + 1u + i),
                &bytes[i])) return;
    }
    lcd_settings_hidden_mask = (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) | ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
    lcd_settings_hidden_mask &= ((1UL << SETTING_COUNT) - 1UL);
    if (magic == SETTINGS_VISIBILITY_OLD_MAGIC) {
        lcd_settings_hidden_mask |= 0x071A4D00u;
        (void)settings_visibility_save(lcd_settings_hidden_mask);
    }
    if (!lcd_settings_has_visible_non_dose_prefix(lcd_settings_hidden_mask)) {
        lcd_settings_hidden_mask = settings_visibility_default_mask();
    }
}

static bool settings_visibility_save(uint32_t mask)
{
    if (!settings_eeprom_write_byte(SETTINGS_VISIBILITY_EEPROM_BASE, 0u)) {
        return false;
    }
    for (uint8_t i = 0u; i < 4u; ++i) {
        if (!settings_eeprom_write_byte(
                (uint16_t)(SETTINGS_VISIBILITY_EEPROM_BASE + 1u + i),
                (uint8_t)(mask >> (8u * i)))) return false;
    }
    return settings_eeprom_write_byte(SETTINGS_VISIBILITY_EEPROM_BASE,
        SETTINGS_VISIBILITY_MAGIC);
}

static void settings_rate_custom_load(void)
{
    (void)settings_value_read(SETTINGS_RATE_CUSTOM_EEPROM_BASE,
        SETTINGS_RATE_CUSTOM_MAGIC, 0xD1u, 9u,
        &lcd_settings_value_index[SETTING_RATE_CUSTOM]);
}

static void settings_rate_custom_save(void)
{
    (void)settings_value_write(SETTINGS_RATE_CUSTOM_EEPROM_BASE,
        SETTINGS_RATE_CUSTOM_MAGIC, 0xD1u,
        lcd_settings_value_index[SETTING_RATE_CUSTOM]);
}

static void settings_dose_actions_load(void)
{
    (void)settings_value_read(SETTINGS_DOSE_SCREEN_EEPROM_BASE,
        0xC2u, 0xD2u, 4u, &lcd_settings_value_index[SETTING_DOSE_SCREEN]);
    (void)settings_value_read(SETTINGS_DOSE_LIGHT_EEPROM_BASE,
        0xC3u, 0xD3u, 4u, &lcd_settings_value_index[SETTING_DOSE_LIGHT]);
    (void)settings_value_read(SETTINGS_DOSE_SOUND_EEPROM_BASE,
        0xC4u, 0xD4u, 4u, &lcd_settings_value_index[SETTING_DOSE_SOUND]);
}

static void settings_dose_action_save(uint8_t setting)
{
    if (setting == SETTING_DOSE_SCREEN) {
        (void)settings_value_write(SETTINGS_DOSE_SCREEN_EEPROM_BASE,
            0xC2u, 0xD2u, lcd_settings_value_index[SETTING_DOSE_SCREEN]);
    } else if (setting == SETTING_DOSE_LIGHT) {
        (void)settings_value_write(SETTINGS_DOSE_LIGHT_EEPROM_BASE,
            0xC3u, 0xD3u, lcd_settings_value_index[SETTING_DOSE_LIGHT]);
    } else if (setting == SETTING_DOSE_SOUND) {
        (void)settings_value_write(SETTINGS_DOSE_SOUND_EEPROM_BASE,
            0xC4u, 0xD4u, lcd_settings_value_index[SETTING_DOSE_SOUND]);
    }
}

static void settings_clicks_load(void)
{
    (void)settings_value_read(SETTINGS_CLICKS_EEPROM_BASE,
        SETTINGS_CLICKS_MAGIC, 0x6Du, 6u,
        &lcd_settings_value_index[SETTING_CLICKS]);
}

static void settings_clicks_save(void)
{
    (void)settings_value_write(SETTINGS_CLICKS_EEPROM_BASE,
        SETTINGS_CLICKS_MAGIC, 0x6Du, lcd_settings_value_index[SETTING_CLICKS]);
}

static void settings_spi_load(void)
{
    uint8_t value;
    if (settings_value_read(SETTINGS_SPI_EEPROM_BASE, SETTINGS_SPI_EEPROM_MAGIC, 0xA5u, 4u, &value)) {
        settings_log_memory = value < 2u ? value : BOARD_LOG_MEMORY_OFF;
        settings_log_source = value & 1u;
        lcd_settings_value_index[SETTING_USE_SPI] = settings_log_memory;
        settings_spi_enabled = settings_log_memory == BOARD_LOG_MEMORY_SPI;
    }
}

static bool settings_spi_save(void)
{
    uint8_t target = lcd_settings_value_index[SETTING_USE_SPI];
    uint8_t source = target < 2u ? target : settings_log_source;
    if (!log_writer_change_memory(target) || !settings_value_write(SETTINGS_SPI_EEPROM_BASE,
            SETTINGS_SPI_EEPROM_MAGIC, 0xA5u, target == BOARD_LOG_MEMORY_OFF ? (uint8_t)(2u + source) : target)) {
        lcd_settings_value_index[SETTING_USE_SPI] = settings_log_memory;
        return false;
    }
    settings_log_memory = target;
    settings_log_source = source;
    settings_spi_enabled = target == BOARD_LOG_MEMORY_SPI;
    log_writer_memory_committed(target);
    return true;
}

static bool settings_long_press_save(void);

static void settings_long_press_load(void)
{
    const uint8_t count = (uint8_t)(sizeof(long_press_milliseconds) /
                                    sizeof(long_press_milliseconds[0]));
    if (settings_value_read(SETTINGS_LONG_PRESS_EEPROM_BASE,
            SETTINGS_LONG_PRESS_MAGIC, 0x37u, count,
            &lcd_settings_value_index[SETTING_LONG_PRESS])) {
        return;
    }

    if (settings_value_read(SETTINGS_LONG_PRESS_LEGACY_EEPROM_BASE,
            SETTINGS_LONG_PRESS_MAGIC, 0x37u, count,
            &lcd_settings_value_index[SETTING_LONG_PRESS])) {
        (void)settings_long_press_save();
        return;
    }

    /* Migrate the previous 500/800/1000/1200/1500 ms option index. */
    uint8_t old_index;
    if (settings_value_read(SETTINGS_LONG_PRESS_LEGACY_EEPROM_BASE,
            SETTINGS_LONG_PRESS_MAGIC, 0x36u, 5u, &old_index)) {
        static const uint8_t migrated_index[] = { 1u, 3u, 3u, 4u, 5u };
        lcd_settings_value_index[SETTING_LONG_PRESS] =
            migrated_index[old_index];
        (void)settings_long_press_save();
    }
}

static bool settings_long_press_save(void)
{
    return settings_value_write(SETTINGS_LONG_PRESS_EEPROM_BASE,
        SETTINGS_LONG_PRESS_MAGIC, 0x37u,
        lcd_settings_value_index[SETTING_LONG_PRESS]);
}

static uint16_t settings_long_press_ms(void)
{
    const uint8_t index = lcd_settings_value_index[SETTING_LONG_PRESS];
    const uint8_t count = (uint8_t)(sizeof(long_press_milliseconds) /
                                    sizeof(long_press_milliseconds[0]));
    return long_press_milliseconds[index < count ? index : 3u];
}

/* Keep legacy EEPROM ordering: old index 1 was MODERN, index 2 CLASSIC. */
static uint8_t settings_tone_swap(uint8_t value)
{
    return value == 1u ? 2u : (value == 2u ? 1u : 0u);
}

static void settings_click_tone_load(void)
{
    uint8_t value;
    if (settings_value_read(SETTINGS_CLICK_TONE_EEPROM_BASE,
            SETTINGS_CLICK_TONE_MAGIC, 0x73u, 3u, &value)) {
        value = settings_tone_swap(value);
        lcd_settings_value_index[SETTING_TONE] = value == 1u ? 0u : value;
    }
}

static void settings_click_tone_save(void)
{
    (void)settings_value_write(SETTINGS_CLICK_TONE_EEPROM_BASE,
        SETTINGS_CLICK_TONE_MAGIC, 0x73u,
        settings_tone_swap(lcd_settings_value_index[SETTING_TONE]));
}

static void settings_sound_alarm_load(void)
{
    (void)settings_value_read(SETTINGS_SOUND_ALARM_EEPROM_BASE,
        SETTINGS_SOUND_ALARM_MAGIC, 0x85u, 4u,
        &lcd_settings_value_index[SETTING_RATE_SOUND]);
}

static void settings_sound_alarm_save(void)
{
    (void)settings_value_write(SETTINGS_SOUND_ALARM_EEPROM_BASE,
        SETTINGS_SOUND_ALARM_MAGIC, 0x85u,
        lcd_settings_value_index[SETTING_RATE_SOUND]);
}

static void settings_alarm_stop_load(void)
{
    (void)settings_value_read(SETTINGS_ALARM_STOP_EEPROM_BASE,
        SETTINGS_ALARM_STOP_MAGIC, 0xA7u, 2u,
        &lcd_settings_value_index[SETTING_ALARM_STOP]);
}

static void settings_alarm_stop_save(void)
{
    (void)settings_value_write(SETTINGS_ALARM_STOP_EEPROM_BASE,
        SETTINGS_ALARM_STOP_MAGIC, 0xA7u,
        lcd_settings_value_index[SETTING_ALARM_STOP]);
}

static bool settings_timer_record_read(uint8_t slot, uint8_t *sequence,
                                       uint8_t *timer_index)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_TIMER_EEPROM_BASE +
        slot * SETTINGS_TIMER_EEPROM_SLOT_SIZE);
    *sequence = record[1];
    *timer_index = record[2];
    return record[0] == SETTINGS_TIMER_EEPROM_MAGIC && *timer_index < 6u &&
        record[3] == (uint8_t)(0x6Bu ^ *sequence ^ (*timer_index * 29u));
}

static void settings_timer_load(void)
{
    uint8_t seq0, timer0, seq1, timer1;
    const bool valid0 = settings_timer_record_read(0u, &seq0, &timer0);
    const bool valid1 = settings_timer_record_read(1u, &seq1, &timer1);

    if (valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0)) {
        lcd_timer_sequence = seq1;
        lcd_settings_value_index[SETTING_SCREEN_TIMER] = timer1;
    } else if (valid0) {
        lcd_timer_sequence = seq0;
        lcd_settings_value_index[SETTING_SCREEN_TIMER] = timer0;
    }
}

static bool settings_timer_save(void)
{
    uint8_t seq0, timer0, seq1, timer1;
    const bool valid0 = settings_timer_record_read(0u, &seq0, &timer0);
    const bool valid1 = settings_timer_record_read(1u, &seq1, &timer1);
    const uint8_t current_slot = valid1 &&
        (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
    const uint8_t target_slot = (valid0 || valid1) ?
        (uint8_t)(current_slot ^ 1u) : 0u;
    const uint8_t sequence = (uint8_t)(lcd_timer_sequence + 1u);
    const uint8_t timer_index = lcd_settings_value_index[SETTING_SCREEN_TIMER];
    const uint8_t base = (uint8_t)(SETTINGS_TIMER_EEPROM_BASE +
        target_slot * SETTINGS_TIMER_EEPROM_SLOT_SIZE);

    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), timer_index) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u),
            (uint8_t)(0x6Bu ^ sequence ^ (timer_index * 29u))) ||
        !settings_eeprom_write_byte(base, SETTINGS_TIMER_EEPROM_MAGIC)) {
        return false;
    }
    lcd_timer_sequence = sequence;
    return true;
}

static bool settings_alarm_record_read(uint8_t slot, uint8_t *sequence,
                                       uint8_t *led_mode,
                                       uint8_t *screen_alarm)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_ALARM_EEPROM_BASE +
        slot * SETTINGS_ALARM_EEPROM_SLOT_SIZE);
    *sequence = record[1];
    *led_mode = record[2];
    *screen_alarm = record[3];
    if (*screen_alarm >= 4u || record[4] != (uint8_t)(0x39u ^ *sequence ^
        (*led_mode * 17u) ^ (*screen_alarm * 29u))) return false;
    if (record[0] == SETTINGS_ALARM_V2_EEPROM_MAGIC) return *led_mode < 6u;
    if (record[0] == SETTINGS_ALARM_EEPROM_MAGIC && *led_mode < 3u) {
        /* Migrate old order ALWAYS/ON/OFF to OFF/ON/ALWAYS. */
        *led_mode = *led_mode == 0u ? 2u :
            (*led_mode == 2u ? 0u : 1u);
        return true;
    }
    return false;
}

static void settings_alarm_load(void)
{
    uint8_t seq0, led0, alarm0, seq1, led1, alarm1;
    const bool valid0 = settings_alarm_record_read(0u, &seq0, &led0, &alarm0);
    const bool valid1 = settings_alarm_record_read(1u, &seq1, &led1, &alarm1);

    if (valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0)) {
        lcd_alarm_sequence = seq1;
        lcd_settings_value_index[SETTING_LED] = led1;
        lcd_settings_value_index[SETTING_RATE_SCREEN] = alarm1;
    } else if (valid0) {
        lcd_alarm_sequence = seq0;
        lcd_settings_value_index[SETTING_LED] = led0;
        lcd_settings_value_index[SETTING_RATE_SCREEN] = alarm0;
    }
}

static bool settings_alarm_save(void)
{
    uint8_t seq0, led0, alarm0, seq1, led1, alarm1;
    const bool valid0 = settings_alarm_record_read(0u, &seq0, &led0, &alarm0);
    const bool valid1 = settings_alarm_record_read(1u, &seq1, &led1, &alarm1);
    const uint8_t current_slot = valid1 &&
        (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
    const uint8_t target_slot = (valid0 || valid1) ?
        (uint8_t)(current_slot ^ 1u) : 0u;
    const uint8_t sequence = (uint8_t)(lcd_alarm_sequence + 1u);
    const uint8_t led_mode = lcd_settings_value_index[SETTING_LED];
    const uint8_t screen_alarm = lcd_settings_value_index[SETTING_RATE_SCREEN];
    const uint8_t base = (uint8_t)(SETTINGS_ALARM_EEPROM_BASE +
        target_slot * SETTINGS_ALARM_EEPROM_SLOT_SIZE);

    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), led_mode) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u), screen_alarm) ||
        !settings_eeprom_write_byte((uint8_t)(base + 4u),
            (uint8_t)(0x39u ^ sequence ^ (led_mode * 17u) ^
                (screen_alarm * 29u))) ||
        !settings_eeprom_write_byte(base, SETTINGS_ALARM_V2_EEPROM_MAGIC)) {
        return false;
    }
    lcd_alarm_sequence = sequence;
    return true;
}

static bool settings_hv_record_read(uint8_t slot, uint8_t *sequence,
                                    uint8_t *hv_mode)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_HV_EEPROM_BASE +
        slot * SETTINGS_HV_EEPROM_SLOT_SIZE);
    *sequence = record[1];
    *hv_mode = record[2];
    return record[0] == SETTINGS_HV_EEPROM_MAGIC && *hv_mode < 2u &&
        record[3] == (uint8_t)(0xC7u ^ *sequence ^ (*hv_mode * 53u));
}

static void settings_hv_load(void)
{
    uint8_t seq0, hv0, seq1, hv1;
    const bool valid0 = settings_hv_record_read(0u, &seq0, &hv0);
    const bool valid1 = settings_hv_record_read(1u, &seq1, &hv1);

    if (valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0)) {
        lcd_hv_sequence = seq1;
        lcd_settings_value_index[SETTING_HV] = hv1;
    } else if (valid0) {
        lcd_hv_sequence = seq0;
        lcd_settings_value_index[SETTING_HV] = hv0;
    }
}

static bool settings_hv_save(void)
{
    uint8_t seq0, hv0, seq1, hv1;
    const bool valid0 = settings_hv_record_read(0u, &seq0, &hv0);
    const bool valid1 = settings_hv_record_read(1u, &seq1, &hv1);
    const uint8_t current_slot = valid1 &&
        (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
    const uint8_t target_slot = (valid0 || valid1) ?
        (uint8_t)(current_slot ^ 1u) : 0u;
    const uint8_t sequence = (uint8_t)(lcd_hv_sequence + 1u);
    const uint8_t hv_mode = lcd_settings_value_index[SETTING_HV];
    const uint8_t base = (uint8_t)(SETTINGS_HV_EEPROM_BASE +
        target_slot * SETTINGS_HV_EEPROM_SLOT_SIZE);

    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), hv_mode) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u),
            (uint8_t)(0xC7u ^ sequence ^ (hv_mode * 53u))) ||
        !settings_eeprom_write_byte(base, SETTINGS_HV_EEPROM_MAGIC)) {
        return false;
    }
    lcd_hv_sequence = sequence;
    return true;
}

static bool settings_light_alarm_record_read(uint8_t slot,
                                             uint8_t *sequence,
                                             uint8_t *mode)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_LIGHT_ALARM_EEPROM_BASE +
        slot * SETTINGS_LIGHT_ALARM_EEPROM_SLOT_SIZE);
    *sequence = record[1];
    *mode = record[2];
    return record[0] == SETTINGS_LIGHT_ALARM_EEPROM_MAGIC && *mode < 4u &&
        record[3] == (uint8_t)(0x5Du ^ *sequence ^ (*mode * 37u));
}

static void settings_light_alarm_load(void)
{
    uint8_t seq0, mode0, seq1, mode1;
    const bool valid0 = settings_light_alarm_record_read(0u, &seq0, &mode0);
    const bool valid1 = settings_light_alarm_record_read(1u, &seq1, &mode1);

    if (valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0)) {
        lcd_light_alarm_sequence = seq1;
        lcd_settings_value_index[SETTING_RATE_LIGHT] = mode1;
    } else if (valid0) {
        lcd_light_alarm_sequence = seq0;
        lcd_settings_value_index[SETTING_RATE_LIGHT] = mode0;
    }
}

static bool settings_light_alarm_save(void)
{
    uint8_t seq0, mode0, seq1, mode1;
    const bool valid0 = settings_light_alarm_record_read(0u, &seq0, &mode0);
    const bool valid1 = settings_light_alarm_record_read(1u, &seq1, &mode1);
    const uint8_t current_slot = valid1 &&
        (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
    const uint8_t target_slot = (valid0 || valid1) ?
        (uint8_t)(current_slot ^ 1u) : 0u;
    const uint8_t sequence = (uint8_t)(lcd_light_alarm_sequence + 1u);
    const uint8_t mode = lcd_settings_value_index[SETTING_RATE_LIGHT];
    const uint8_t base = (uint8_t)(SETTINGS_LIGHT_ALARM_EEPROM_BASE +
        target_slot * SETTINGS_LIGHT_ALARM_EEPROM_SLOT_SIZE);

    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), mode) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u),
            (uint8_t)(0x5Du ^ sequence ^ (mode * 37u))) ||
        !settings_eeprom_write_byte(base, SETTINGS_LIGHT_ALARM_EEPROM_MAGIC)) {
        return false;
    }
    lcd_light_alarm_sequence = sequence;
    return true;
}

static bool settings_dose_alarm_record_read(uint8_t slot,
                                            uint8_t *sequence,
                                            uint8_t *mode)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_DOSE_ALARM_EEPROM_BASE +
        slot * SETTINGS_DOSE_ALARM_EEPROM_SLOT_SIZE);
    const uint8_t stored_mode = record[2];
    *sequence = record[1];
    if (record[3] != (uint8_t)(0xA3u ^ *sequence ^ (stored_mode * 23u))) {
        return false;
    }
    if (record[0] == 0xC5u && stored_mode < 8u) {
        *mode = stored_mode;
        return true;
    }
    if (record[0] == SETTINGS_DOSE_ALARM_EEPROM_MAGIC && stored_mode < 11u) {
        /* Preserve old fixed dose limits; AUTO/OFF migrate to 5 uSv. */
        *mode = stored_mode < 3u ? 0u : (uint8_t)(stored_mode - 3u);
        return true;
    }
    return false;
}

static void settings_dose_alarm_load(void)
{
    uint8_t seq0, mode0, seq1, mode1;
    const bool valid0 = settings_dose_alarm_record_read(0u, &seq0, &mode0);
    const bool valid1 = settings_dose_alarm_record_read(1u, &seq1, &mode1);

    if (valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0)) {
        lcd_dose_alarm_sequence = seq1;
        lcd_settings_value_index[SETTING_DOSE_CUSTOM] = mode1;
    } else if (valid0) {
        lcd_dose_alarm_sequence = seq0;
        lcd_settings_value_index[SETTING_DOSE_CUSTOM] = mode0;
    }
}

static bool settings_dose_alarm_save(void)
{
    uint8_t seq0, mode0, seq1, mode1;
    const bool valid0 = settings_dose_alarm_record_read(0u, &seq0, &mode0);
    const bool valid1 = settings_dose_alarm_record_read(1u, &seq1, &mode1);
    const uint8_t current_slot = valid1 &&
        (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
    const uint8_t target_slot = (valid0 || valid1) ?
        (uint8_t)(current_slot ^ 1u) : 0u;
    const uint8_t sequence = (uint8_t)(lcd_dose_alarm_sequence + 1u);
    const uint8_t mode = lcd_settings_value_index[SETTING_DOSE_CUSTOM];
    const uint8_t base = (uint8_t)(SETTINGS_DOSE_ALARM_EEPROM_BASE +
        target_slot * SETTINGS_DOSE_ALARM_EEPROM_SLOT_SIZE);

    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), mode) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u),
            (uint8_t)(0xA3u ^ sequence ^ (mode * 23u))) ||
        !settings_eeprom_write_byte(base, 0xC5u)) {
        return false;
    }
    lcd_dose_alarm_sequence = sequence;
    return true;
}

static void settings_eeprom_load(void)
{
    uint8_t seq0, lead0, unit0, rate0, dose0, led0, screen0, time0;
    uint8_t seq1, lead1, unit1, rate1, dose1, led1, screen1, time1;
    bool valid0 = settings_v3_record_read(0u, &seq0, &lead0, &unit0,
        &rate0, &dose0, &led0, &screen0, &time0);
    bool valid1 = settings_v3_record_read(1u, &seq1, &lead1, &unit1,
        &rate1, &dose1, &led1, &screen1, &time1);
    uint8_t slot;

    if (valid0 || valid1) {
        slot = valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
        if (slot == 0u) {
            lcd_settings_sequence = seq0;
            lcd_settings_value_index[SETTING_LEAD_ZERO] = lead0;
            lcd_settings_value_index[SETTING_UNIT] = unit0;
            lcd_settings_value_index[SETTING_RATE_PREFIX] = rate0;
            lcd_settings_value_index[SETTING_DOSE_PREFIX] = dose0;
            lcd_settings_value_index[SETTING_LED_BRIGHT] = led0;
            lcd_settings_value_index[SETTING_SCREEN_BRIGHT] = screen0;
            lcd_settings_value_index[SETTING_LED_TIME] = time0;
        } else {
            lcd_settings_sequence = seq1;
            lcd_settings_value_index[SETTING_LEAD_ZERO] = lead1;
            lcd_settings_value_index[SETTING_UNIT] = unit1;
            lcd_settings_value_index[SETTING_RATE_PREFIX] = rate1;
            lcd_settings_value_index[SETTING_DOSE_PREFIX] = dose1;
            lcd_settings_value_index[SETTING_LED_BRIGHT] = led1;
            lcd_settings_value_index[SETTING_SCREEN_BRIGHT] = screen1;
            lcd_settings_value_index[SETTING_LED_TIME] = time1;
        }
        return;
    }
    valid0 = settings_v2_record_read(0u, &seq0, &lead0, &unit0,
                                     &rate0, &dose0);
    valid1 = settings_v2_record_read(1u, &seq1, &lead1, &unit1,
                                     &rate1, &dose1);
    if (valid0 || valid1) {
        slot = valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
        if (slot == 0u) {
            lcd_settings_sequence = seq0;
            lcd_settings_value_index[SETTING_LEAD_ZERO] = lead0;
            lcd_settings_value_index[SETTING_UNIT] = unit0;
            lcd_settings_value_index[SETTING_RATE_PREFIX] = rate0;
            lcd_settings_value_index[SETTING_DOSE_PREFIX] = dose0;
        } else {
            lcd_settings_sequence = seq1;
            lcd_settings_value_index[SETTING_LEAD_ZERO] = lead1;
            lcd_settings_value_index[SETTING_UNIT] = unit1;
            lcd_settings_value_index[SETTING_RATE_PREFIX] = rate1;
            lcd_settings_value_index[SETTING_DOSE_PREFIX] = dose1;
        }
        return;
    }
    /* Migrate values written by the original two-option settings record. */
    if (settings_legacy_record_read(0u, &seq0, &lead0, &unit0) ||
        settings_legacy_record_read(1u, &seq1, &lead1, &unit1)) {
        if (settings_legacy_record_read(1u, &seq1, &lead1, &unit1) &&
            (!settings_legacy_record_read(0u, &seq0, &lead0, &unit0) ||
             (int8_t)(seq1 - seq0) > 0)) {
            lcd_settings_sequence = seq1;
            lcd_settings_value_index[SETTING_LEAD_ZERO] = lead1;
            lcd_settings_value_index[SETTING_UNIT] = unit1;
        } else {
            lcd_settings_sequence = seq0;
            lcd_settings_value_index[SETTING_LEAD_ZERO] = lead0;
            lcd_settings_value_index[SETTING_UNIT] = unit0;
        }
        lcd_settings_value_index[SETTING_RATE_PREFIX] =
            lcd_settings_value_index[SETTING_UNIT] == 1u ? 0u : 1u;
        lcd_settings_value_index[SETTING_DOSE_PREFIX] = lcd_settings_value_index[SETTING_RATE_PREFIX];
    }
    settings_row_load();
}

static bool settings_eeprom_save(void)
{
    uint8_t seq0, lead0, unit0, rate0, dose0, led0, screen0, time0;
    uint8_t seq1, lead1, unit1, rate1, dose1, led1, screen1, time1;
    const bool valid0 = settings_v3_record_read(0u, &seq0, &lead0, &unit0,
        &rate0, &dose0, &led0, &screen0, &time0);
    const bool valid1 = settings_v3_record_read(1u, &seq1, &lead1, &unit1,
        &rate1, &dose1, &led1, &screen1, &time1);
    const uint8_t current_slot = valid1 &&
        (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
    const uint8_t target_slot =
        (valid0 || valid1) ? (uint8_t)(current_slot ^ 1u) : 0u;
    const uint8_t sequence = (uint8_t)(lcd_settings_sequence + 1u);
    const uint8_t lead_zero = lcd_settings_value_index[SETTING_LEAD_ZERO];
    const uint8_t unit = lcd_settings_value_index[SETTING_UNIT];
    const uint8_t rate_prefix = lcd_settings_value_index[SETTING_RATE_PREFIX];
    const uint8_t dose_prefix = lcd_settings_value_index[SETTING_DOSE_PREFIX];
    const uint8_t led_brightness = lcd_settings_value_index[SETTING_LED_BRIGHT];
    const uint8_t led_time = lcd_settings_value_index[SETTING_LED_TIME];
    const uint8_t screen_brightness = lcd_settings_value_index[SETTING_SCREEN_BRIGHT];
    const uint8_t base = (uint8_t)(SETTINGS_EEPROM_V3_BASE +
        target_slot * SETTINGS_EEPROM_V3_SLOT_SIZE);

    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), lead_zero) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u), unit) ||
        !settings_eeprom_write_byte((uint8_t)(base + 4u), rate_prefix) ||
        !settings_eeprom_write_byte((uint8_t)(base + 5u), dose_prefix) ||
        !settings_eeprom_write_byte((uint8_t)(base + 6u),
            (uint8_t)((led_time << 4) | led_brightness)) ||
        !settings_eeprom_write_byte((uint8_t)(base + 7u), screen_brightness) ||
        !settings_eeprom_write_byte((uint8_t)(base + 8u),
            settings_v3_crc(sequence, lead_zero, unit, rate_prefix,
                dose_prefix, (uint8_t)((led_time << 4) | led_brightness),
                screen_brightness)) ||
        !settings_eeprom_write_byte(base, SETTINGS_EEPROM_V4_MAGIC)) {
        return false;
    }
    lcd_settings_sequence = sequence;
    return true;
}

bool board_display_settings_list_active(void)
{
    return lcd_screen_page == 2u && lcd_settings_list_active;
}

static void lcd_settings_apply_current(void)
{
    if (lcd_settings_selected == 9u) {
        const uint8_t prefix_default =
            lcd_settings_value_index[SETTING_UNIT] == 1u ? 0u : 1u;
        lcd_settings_value_index[SETTING_RATE_PREFIX] = prefix_default;
        lcd_settings_value_index[SETTING_DOSE_PREFIX] = prefix_default;
        if (lcd_settings_value_index[SETTING_DOSE_ROW_2] >=
                lcd_settings_value_count(SETTING_DOSE_ROW_2)) {
            lcd_settings_value_index[SETTING_DOSE_ROW_2] = 0u;
        }
        if (lcd_settings_value_index[SETTING_RATE_ROW_2] >=
                lcd_settings_value_count(SETTING_RATE_ROW_2)) {
            lcd_settings_value_index[SETTING_RATE_ROW_2] = 0u;
        }
        lcd_dose_row_hidden = false;
        lcd_format_cpm(lcd_current_cpm);
        lcd_format_dose_row();
        (void)settings_row_save();
    }
    if (lcd_settings_selected >= 8u && lcd_settings_selected <= 11u) {
        (void)settings_eeprom_save();
        if (lcd_screen_page == 0u) {
            lcd_format_rate_display(lcd_rate_value_thousandths);
        } else if (lcd_screen_page == 1u) {
            lcd_format_dose_display(lcd_dose_value_thousandths,
                                    lcd_dose_fraction);
            lcd_format_dose_row();
        }
        lcd_cpm_redraw_pending = true;
    }
    if (lcd_settings_selected == 12u) {
        lcd_format_cpm(lcd_current_cpm);
        (void)settings_row_save();
        lcd_cpm_redraw_pending = true;
    }
    if (lcd_settings_selected == SETTING_DOSE_ROW_2 ||
        lcd_settings_selected == SETTING_DOSE_PERIOD) {
        if (lcd_settings_selected == SETTING_DOSE_PERIOD) {
            lcd_settings_value_index[SETTING_DOSE_ROW_2] =
                lcd_settings_value_index[SETTING_DOSE_PERIOD] == 0u ? 3u : 1u;
        }
        if (lcd_settings_value_index[SETTING_DOSE_ROW_2] >=
                lcd_dose_row_option_count()) {
            lcd_settings_value_index[SETTING_DOSE_ROW_2] = 0u;
        }
        lcd_dose_row_hidden = false;
        (void)settings_row_save();
        lcd_format_dose_row();
        lcd_cpm_redraw_pending = true;
    }
    if (lcd_settings_selected == SETTING_LED_BRIGHT ||
        lcd_settings_selected == SETTING_SCREEN_BRIGHT ||
        lcd_settings_selected == SETTING_LED_TIME) {
        (void)settings_eeprom_save();
        lcd_apply_brightness_settings();
    }
    if (lcd_settings_selected == 7u) settings_timer_save();
    if (lcd_settings_selected == 1u) settings_clicks_save();
    if (lcd_settings_selected == 4u) settings_sound_alarm_save();
    if (lcd_settings_selected == 20u) settings_click_tone_save();
    if (lcd_settings_selected == SETTING_LONG_PRESS) {
        (void)settings_long_press_save();
    }
    if (lcd_settings_selected == 17u) settings_alarm_stop_save();
    if (lcd_settings_selected == 13u) {
        (void)settings_spi_save();
        if (settings_log_source != BOARD_LOG_MEMORY_SPI) spi_nor_deinit();
    }
    if (lcd_settings_selected == 0u || lcd_settings_selected == 2u) {
        (void)settings_alarm_save();
    }
    if (lcd_settings_selected == 3u) {
        (void)settings_light_alarm_save();
    }
    if (lcd_settings_selected == SETTING_RATE_CUSTOM) {
        settings_rate_custom_save();
    }
    if (lcd_settings_selected == SETTING_DOSE_CUSTOM) {
        (void)settings_dose_alarm_save();
    }
    if (lcd_settings_selected == SETTING_DOSE_SCREEN ||
        lcd_settings_selected == SETTING_DOSE_LIGHT ||
        lcd_settings_selected == SETTING_DOSE_SOUND) {
        settings_dose_action_save(lcd_settings_selected);
    }
    if (lcd_settings_selected == 15u) {
        board_hv_set_enabled(lcd_settings_value_index[SETTING_HV] == 0u);
        (void)settings_hv_save();
        if (lcd_settings_value_index[SETTING_HV] == 0u) {
            lcd_rate_value_thousandths = 0u;
            lcd_current_cpm = 0u;
            lcd_rate_has_measurement = false;
            lcd_dose_has_measurement = false;
            lcd_format_rate_display(0u);
            lcd_format_cpm(0u);
        }
        lcd_text_active = false;
        lcd_text_row_commands_queued = false;
        lcd_background_pending = true;
        lcd_cpm_redraw_pending = true;
    }
}

uint8_t board_display_settings_remote_count(void)
{
    return SETTING_COUNT;
}

bool board_display_settings_remote_read(uint8_t *values, uint8_t capacity)
{
    if (values == NULL || capacity < SETTING_COUNT) return false;
    for (uint8_t i = 0u; i < SETTING_COUNT; ++i) {
        values[i] = lcd_settings_value_index[i];
    }
    return true;
}

uint32_t board_display_settings_hidden_mask(void)
{
    return lcd_settings_hidden_mask;
}

bool board_display_setting_remote_write(uint8_t setting_id, uint8_t value)
{
    const uint8_t previous_selected = lcd_settings_selected;
    const uint8_t previous_edit_value = lcd_settings_edit_value;
    const bool previous_editing = lcd_settings_editing;

    if (setting_id >= 0x80u && setting_id < (uint8_t)(0x80u + SETTING_COUNT)) {
        const uint8_t item = (uint8_t)(setting_id - 0x80u);
        const uint32_t bit = 1UL << item;
        const uint32_t candidate = value != 0u ?
            (lcd_settings_hidden_mask | bit) :
            (lcd_settings_hidden_mask & ~bit);
        if (LCD_CLEANUP_MENU_ACTIVE || value > 1u ||
            lcd_settings_count_visible_for_mask(candidate) == 0u) return false;
        if (candidate == lcd_settings_hidden_mask) return true;
        if (!settings_visibility_save(candidate)) return false;
        lcd_settings_hidden_mask = candidate;
        if (lcd_screen_page == 3u) {
            if ((candidate & (1UL << lcd_settings_selected)) != 0u) {
                lcd_settings_selected = lcd_settings_visible_to_item(0u);
                lcd_settings_editing = false;
            }
            if (board_display_settings_list_active()) {
                board_display_settings_redraw();
            }
        }
        return true;
    }

    /* RESET, POWER OFF and REBOOT are actions, not remote preferences. */
    if (lcd_settings_list_active || setting_id >= SETTING_COUNT ||
        setting_id == SETTING_RESET ||
        setting_id == SETTING_POWER_OFF || setting_id == SETTING_REBOOT ||
        value >= lcd_settings_value_count(setting_id)) {
        return false;
    }

    if (setting_id == SETTING_UNIT && value == 1u &&
        !lcd_settings_has_visible_non_dose_prefix(lcd_settings_hidden_mask)) {
        return false;
    }

    if (lcd_settings_value_index[setting_id] == value) return true;

    const uint8_t previous_value = lcd_settings_value_index[setting_id];
    lcd_settings_selected = setting_id;
    lcd_settings_editing = false;
    lcd_settings_value_index[setting_id] = value;
    lcd_settings_apply_current();
    if (setting_id == SETTING_USE_SPI &&
        lcd_settings_value_index[setting_id] != value) {
        lcd_settings_value_index[setting_id] = previous_value;
        lcd_settings_selected = previous_selected;
        lcd_settings_edit_value = previous_edit_value;
        lcd_settings_editing = previous_editing;
        return false;
    }
    lcd_settings_selected = previous_selected;
    lcd_settings_edit_value = previous_edit_value;
    lcd_settings_editing = previous_editing;
    return true;
}

static bool settings_reset_all(void)
{
    static const uint8_t record_magic_offsets[] = {
        0u, 5u, 16u, 23u, 32u, 41u, SETTINGS_ROW_EEPROM_BASE,
        SETTINGS_ROW_EEPROM_BASE + SETTINGS_ROW_EEPROM_SLOT_SIZE,
        64u, 68u, 80u, 85u,
        96u, 100u, 104u, 108u, 112u, 116u, 120u, 124u, 128u, 132u,
        SETTINGS_SPI_EEPROM_BASE, SETTINGS_LONG_PRESS_EEPROM_BASE,
        SETTINGS_RATE_CUSTOM_EEPROM_BASE, SETTINGS_DOSE_SCREEN_EEPROM_BASE,
        SETTINGS_DOSE_LIGHT_EEPROM_BASE, SETTINGS_DOSE_SOUND_EEPROM_BASE,
        SETTINGS_VISIBILITY_EEPROM_BASE
    };

    for (uint8_t i = 0u;
         i < sizeof(record_magic_offsets) / sizeof(record_magic_offsets[0]);
         ++i) {
        if (!settings_eeprom_write_byte(record_magic_offsets[i], 0u)) {
            return false;
        }
    }
    return settings_value_write(SETTINGS_LONG_PRESS_EEPROM_BASE,
        SETTINGS_LONG_PRESS_MAGIC, 0x37u, 3u);
}

static bool lcd_cleanup_clear_spi(void)
{
    return (spi_nor_is_ready() || spi_nor_init()) && spi_log_clear();
}

static bool lcd_cleanup_execute(void)
{
    bool ok = false;
    switch (lcd_cleanup_selected) {
    case 0u:
        ok = settings_reset_all();
        if (ok) board_reboot();
        return ok;
    case 1u:
        ok = log_eeprom_reset(false) && lcd_cleanup_clear_spi();
        if (ok) board_reboot();
        return ok;
    case 2u:
        if (settings_log_memory == BOARD_LOG_MEMORY_SPI) {
            ok = log_eeprom_reset(false);
        } else if (settings_log_memory == BOARD_LOG_MEMORY_EEPROM) {
            ok = lcd_cleanup_clear_spi();
        } else {
            ok = log_eeprom_reset(false) && lcd_cleanup_clear_spi();
        }
        if (ok) board_reboot();
        return ok;
    case 3u:
        ok = board_eeprom_clear_all();
        if (ok) board_reset();
        return ok;
    case 4u:
        ok = lcd_cleanup_clear_spi();
        if (ok) board_reboot();
        return ok;
    case 5u:
        ok = board_eeprom_clear_all() && lcd_cleanup_clear_spi();
        if (ok) board_reset();
        return ok;
    default:
        return false;
    }
    return ok;
}

bool board_display_remote_action_valid(uint8_t action, uint8_t value)
{
    if (action == PROTOCOL_V1_ACTION_RESET) {
        return value < LCD_RESET_COUNT && !LCD_CLEANUP_MENU_ACTIVE;
    }
    if (action == PROTOCOL_V1_ACTION_POWER_OFF ||
        action == PROTOCOL_V1_ACTION_REBOOT) {
        return value == 0u;
    }
    return false;
}

void board_display_remote_action_execute(uint8_t action, uint8_t value)
{
    if (action == PROTOCOL_V1_ACTION_RESET) {
        lcd_cleanup_selected = value;
        (void)lcd_cleanup_execute();
    } else if (action == PROTOCOL_V1_ACTION_POWER_OFF) {
        board_power_off();
    } else if (action == PROTOCOL_V1_ACTION_REBOOT) {
        board_reboot();
    }
}

uint32_t board_display_screen_timeout_ms(void)
{
    static const uint8_t timeout_seconds[] = { 1u, 5u, 10u, 30u, 60u, 90u };
    if (lcd_indication_mode == 0u) return 1000u;
    return (uint32_t)timeout_seconds[lcd_settings_value_index[SETTING_SCREEN_TIMER]] * 1000u;
}

bool board_display_cleanup_feedback_active(void)
{
    return lcd_cleanup_status == 1u || lcd_cleanup_status == 3u;
}

static void lcd_cleanup_dismiss_success(void)
{
    if (lcd_cleanup_status == 1u) {
        lcd_cleanup_feedback_dismissed = true;
    }
    lcd_cleanup_status = 0u;
}

bool board_display_take_cleanup_feedback_dismissed(void)
{
    const bool dismissed = lcd_cleanup_feedback_dismissed;
    lcd_cleanup_feedback_dismissed = false;
    return dismissed;
}

static bool rate_custom_threshold_active(void)
{
    static const uint16_t custom_rate_tenths_usvh[] = {
        3u, 4u, 5u, 7u, 9u, 10u, 20u, 50u, 100u
    };
    const uint8_t custom = lcd_settings_value_index[SETTING_RATE_CUSTOM];
    return lcd_alarm_rate_window_ready &&
        lcd_alarm_rate_value_thousandths >=
            (uint32_t)custom_rate_tenths_usvh[custom] * 100u;
}

void board_display_set_rate_alarm(uint32_t rate_thousandths, uint32_t cpm,
                                  bool window_ready)
{
    const uint64_t rate_scaled = (uint64_t)cpm * 666u;
    uint16_t color = LCD_COUNTER_BLUE;
    lcd_alarm_rate_value_thousandths = rate_thousandths;
    lcd_alarm_rate_window_ready = window_ready;
    if (window_ready) {
        color = LCD_COUNTER_GREEN;
        if (rate_scaled >= 40000u && rate_scaled <= 90000u) {
            color = LCD_COUNTER_YELLOW;
        } else if (rate_scaled > 90000u) {
            color = LCD_COUNTER_RED;
        }
    }
    lcd_alarm_rate_color = color;
}

static bool indication_enabled(uint8_t mode, bool screen_on)
{
    if (lcd_indication_mode == 0u) return false;
    if (mode == 1u) return screen_on;
    if (mode == 2u) return true;
    if (mode == 3u) return lcd_alarm_rate_color == LCD_COUNTER_YELLOW ||
        lcd_alarm_rate_color == LCD_COUNTER_RED;
    if (mode == 4u) return lcd_alarm_rate_color == LCD_COUNTER_RED;
    if (mode == 5u) return rate_custom_threshold_active();
    return false;
}

bool board_display_led_flash_enabled(bool screen_on)
{
    if (lcd_indication_mode == 2u) return true;
    return indication_enabled(lcd_settings_value_index[SETTING_LED], screen_on);
}

bool board_display_led_auto(void)
{
    return lcd_settings_value_index[SETTING_LED_BRIGHT] == 0u ||
        lcd_settings_value_index[SETTING_LED_TIME] == 0u;
}

bool board_display_click_sound_enabled(bool screen_on)
{
    return indication_enabled(lcd_settings_value_index[SETTING_CLICKS], screen_on);
}

static void board_sound_timer_config(void)
{
    const bool lower_tone = sound_alarm_mode &&
        ((sound_alarm_phase == 0u) == (click_sound_profile == 1u));
    /* Keep roughly the same audible frequencies at 131 kHz and 32 MHz. */
    REG32(TIM2_BASE + TIM_PSC) = idle_clock_active ? 0u :
        (lower_tone ? 31u : 22u);
    REG32(TIM2_BASE + TIM_ARR) = idle_clock_active ?
        (lower_tone ? 40u : 29u) : 310u;
    REG32(TIM2_BASE + TIM_CCR4) = idle_clock_active ?
        (lower_tone ? 7u : 5u) : 50u;
    REG32(TIM2_BASE + TIM_CNT) = 0u;
    board_rgb_apply();
}

static void board_click_sound_stop(void)
{
    REG32(TIM2_BASE + TIM_CCER) &= ~(1u << 12u);
    REG32(TIM2_BASE + TIM_CCR4) = 0u;
    REG32(TIM2_BASE + TIM_PSC) = 0u;
    REG32(TIM2_BASE + TIM_ARR) = PWM_PERIOD - 1u;
    click_sound_active = false;
    board_rgb_apply();
    REG32(GPIOA_BASE + GPIO_MODER) &= ~(3u << 6u);
    pin_input_with_pull(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN);
}

void board_click_sound_start(void)
{
    REG32(GPIOA_BASE + GPIO_PUPDR) &= ~(3u << 6u);
    REG32(GPIOA_BASE + GPIO_OTYPER) &= ~(1u << 3u);
    REG32(GPIOA_BASE + GPIO_OSPEEDR) &= ~(3u << 6u);
    REG32(GPIOA_BASE + GPIO_AFRL) =
        (REG32(GPIOA_BASE + GPIO_AFRL) & ~(0xFu << 12u)) | (2u << 12u);
    REG32(GPIOA_BASE + GPIO_MODER) =
        (REG32(GPIOA_BASE + GPIO_MODER) & ~(3u << 6u)) | (2u << 6u);

    RCC_APB1ENR |= 1u; /* TIM2 may be clock-gated while RGB is off. */
    (void)RCC_APB1ENR;
    sound_alarm_mode = false;
    click_sound_profile = lcd_settings_value_index[SETTING_TONE];
    if (click_sound_profile == 1u && click_sound_active) return;
    REG32(TIM2_BASE + TIM_CCMR2) =
        (REG32(TIM2_BASE + TIM_CCMR2) & ~(0xFFu << 8u)) |
        (6u << 12u) | (1u << 11u); /* CH4 PWM1, preload enabled. */
    REG32(TIM2_BASE + TIM_CCER) |= 1u << 12u;
    click_sound_active = true;
    board_sound_timer_config();
    click_sound_deadline = board_millis +
        (click_sound_profile == 1u ? 2u : 38u);
}

void board_sound_alarm_set(bool active)
{
    if (!active) {
        if (sound_alarm_mode) board_click_sound_stop();
        sound_alarm_mode = false;
        return;
    }
    if (sound_alarm_mode) return;

    sound_alarm_mode = true;
    click_sound_active = true;
    click_sound_profile = lcd_settings_value_index[SETTING_TONE];
    sound_alarm_phase = 0u;
    REG32(GPIOA_BASE + GPIO_PUPDR) &= ~(3u << 6u);
    REG32(GPIOA_BASE + GPIO_OTYPER) &= ~(1u << 3u);
    REG32(GPIOA_BASE + GPIO_OSPEEDR) &= ~(3u << 6u);
    REG32(GPIOA_BASE + GPIO_AFRL) =
        (REG32(GPIOA_BASE + GPIO_AFRL) & ~(0xFu << 12u)) | (2u << 12u);
    REG32(GPIOA_BASE + GPIO_MODER) =
        (REG32(GPIOA_BASE + GPIO_MODER) & ~(3u << 6u)) | (2u << 6u);
    RCC_APB1ENR |= 1u; /* TIM2 may be clock-gated while RGB is off. */
    (void)RCC_APB1ENR;
    REG32(TIM2_BASE + TIM_CCMR2) =
        (REG32(TIM2_BASE + TIM_CCMR2) & ~(0xFFu << 8u)) |
        (6u << 12u) | (1u << 11u);
    REG32(TIM2_BASE + TIM_CCER) |= 1u << 12u;
    board_sound_timer_config();
    click_sound_deadline = board_millis +
        (click_sound_profile == 1u ? 240u : 400u);
}

void board_click_sound_service(void)
{
    if (sound_alarm_mode &&
        (int32_t)(board_millis - click_sound_deadline) >= 0) {
        sound_alarm_phase ^= 1u;
        click_sound_deadline += click_sound_profile == 1u ? 240u : 400u;
        board_sound_timer_config();
    } else if (click_sound_active && !sound_alarm_mode &&
        (int32_t)(board_millis - click_sound_deadline) >= 0) {
        board_click_sound_stop();
    }
}

bool board_click_sound_busy(void)
{
    return click_sound_active;
}

static bool rate_alarm_for_mode(uint8_t mode)
{
    if (mode == 1u) {
        return lcd_alarm_rate_color == LCD_COUNTER_YELLOW ||
            lcd_alarm_rate_color == LCD_COUNTER_RED;
    }
    if (mode == 2u) return lcd_alarm_rate_color == LCD_COUNTER_RED;
    if (mode == 3u) return rate_custom_threshold_active();
    return false;
}

static bool dose_alarm_for_mode(uint8_t mode)
{
    static const uint16_t custom_dose_usv[] = {
        5u, 7u, 10u, 30u, 50u, 100u, 500u, 1000u
    };
    if (lcd_dose_measurement_seconds == 0u) return false;
    const uint64_t actual_nsv_x3600 =
        (uint64_t)lcd_dose_value_thousandths * 3600u + lcd_dose_fraction;
    uint64_t threshold_nsv_x3600;

    if (mode == 1u || mode == 2u) {
        threshold_nsv_x3600 = (uint64_t)(mode == 1u ? 400u : 900u) *
            lcd_dose_measurement_seconds;
    } else if (mode == 3u) {
        const uint8_t custom = lcd_settings_value_index[SETTING_DOSE_CUSTOM];
        threshold_nsv_x3600 = (uint64_t)custom_dose_usv[custom] *
            1000u * 3600u;
    } else {
        return false;
    }
    return actual_nsv_x3600 >= threshold_nsv_x3600;
}

static bool display_alarm_active(uint8_t rate_mode, uint8_t dose_mode)
{
    if (lcd_indication_mode == 0u || lcd_settings_value_index[SETTING_HV] != 0u)
        return false;
    return rate_alarm_for_mode(rate_mode) || dose_alarm_for_mode(dose_mode);
}

bool board_display_screen_alarm_active(void)
{
    return display_alarm_active(
        lcd_settings_value_index[SETTING_RATE_SCREEN],
        lcd_settings_value_index[SETTING_DOSE_SCREEN]);
}

bool board_display_light_alarm_active(void)
{
    return display_alarm_active(
        lcd_settings_value_index[SETTING_RATE_LIGHT],
        lcd_settings_value_index[SETTING_DOSE_LIGHT]);
}

bool board_display_dose_alarm_active(void)
{
    return dose_alarm_for_mode(lcd_settings_value_index[SETTING_DOSE_SCREEN]) ||
        dose_alarm_for_mode(lcd_settings_value_index[SETTING_DOSE_LIGHT]) ||
        dose_alarm_for_mode(lcd_settings_value_index[SETTING_DOSE_SOUND]);
}

bool board_display_sound_alarm_active(void)
{
    return display_alarm_active(
        lcd_settings_value_index[SETTING_RATE_SOUND],
        lcd_settings_value_index[SETTING_DOSE_SOUND]);
}

bool board_display_sound_alarm_stop_on_clear(void)
{
    return lcd_indication_mode == 0u || lcd_settings_value_index[SETTING_ALARM_STOP] == 0u ||
        (lcd_settings_value_index[SETTING_RATE_SOUND] == 0u &&
         lcd_settings_value_index[SETTING_DOSE_SOUND] == 0u) ||
        lcd_settings_value_index[SETTING_HV] != 0u;
}

void board_display_settings_list_activate(void)
{
    if (lcd_screen_page == 3u) {
        lcd_debug_subpage = (uint8_t)((lcd_debug_subpage + 1u) %
            LCD_DEBUG_SUBPAGE_COUNT);
        lcd_text_active = false;
        lcd_text_row_commands_queued = false;
        lcd_background_pending = true;
        lcd_cpm_redraw_pending = true;
        return;
    }
    if (lcd_screen_page != 2u ||
        lcd_settings_list_active) return;
    lcd_settings_list_active = true;
    lcd_settings_editing = false;
    board_display_settings_redraw();
}

static void board_display_settings_redraw(void)
{
    lcd_settings_redraw_pending = true;
}

void board_display_settings_move(bool forward)
{
    if (!board_display_settings_list_active()) return;
    if (lcd_settings_spi_failed) lcd_settings_spi_failed = false;
    if (LCD_CLEANUP_MENU_ACTIVE) {
        lcd_cleanup_dismiss_success();
        lcd_cleanup_selected = forward ?
            (uint8_t)((lcd_cleanup_selected + 1u) % LCD_CLEANUP_COUNT) :
            (uint8_t)((lcd_cleanup_selected + LCD_CLEANUP_COUNT - 1u) %
                LCD_CLEANUP_COUNT);
        board_display_settings_redraw();
        return;
    }
    if (lcd_settings_editing) {
        if (lcd_settings_selected == SETTING_RESET) {
            lcd_cleanup_status = 0u;
        }
        const uint8_t count = lcd_settings_value_count(lcd_settings_selected);
        lcd_settings_edit_value = forward ?
            (uint8_t)((lcd_settings_edit_value + 1u) % count) :
            (uint8_t)((lcd_settings_edit_value + count - 1u) % count);
        board_display_settings_redraw();
        return;
    }
    {
        const uint8_t visible_count = lcd_settings_visible_count();
        uint8_t visible = lcd_settings_item_to_visible(lcd_settings_selected);
        visible = forward ? (uint8_t)((visible + 1u) % visible_count) :
            (uint8_t)((visible + visible_count - 1u) % visible_count);
        lcd_settings_selected = lcd_settings_visible_to_item(visible);
    }
    board_display_settings_redraw();
}

void board_display_settings_change(void)
{
    if (!board_display_settings_list_active()) return;
    if (LCD_CLEANUP_MENU_ACTIVE) {
        if (!LCD_CLEANUP_CONFIRM_PENDING) {
            lcd_cleanup_dismiss_success();
        } else {
            lcd_cleanup_working_pending = true;
            lcd_cleanup_status = 3u;
        }
        board_display_settings_redraw();
        return;
    }
    if (lcd_settings_selected == LCD_SETTINGS_POWER_OFF_ITEM) {
        board_power_off();
        return;
    }
    if (lcd_settings_selected == LCD_SETTINGS_REBOOT_ITEM) {
        board_reboot();
        return;
    }
    if (lcd_settings_value_count(lcd_settings_selected) > 3u ||
        lcd_settings_selected == SETTING_USE_SPI) {
        if (!lcd_settings_editing) {
            lcd_settings_edit_value =
                lcd_settings_value_index[lcd_settings_selected];
            lcd_settings_editing = true;
            if (lcd_settings_selected == SETTING_USE_SPI) {
                lcd_settings_spi_failed = false;
            }
            if (lcd_settings_selected == SETTING_RESET) {
                lcd_cleanup_status = 0u;
            }
        } else if (lcd_settings_selected == SETTING_RESET) {
            lcd_cleanup_working_pending = true;
            lcd_cleanup_selected = lcd_settings_edit_value;
            lcd_cleanup_status = 3u;
            lcd_settings_editing = false;
        } else {
            const uint8_t requested_value = lcd_settings_edit_value;
            lcd_settings_value_index[lcd_settings_selected] =
                requested_value;
            lcd_settings_editing = false;
            lcd_settings_apply_current();
            lcd_settings_spi_failed =
                lcd_settings_selected == SETTING_USE_SPI &&
                lcd_settings_value_index[SETTING_USE_SPI] != requested_value;
        }
        board_display_settings_redraw();
        return;
    }
    if (lcd_settings_value_count(lcd_settings_selected) > 1u) {
        lcd_settings_value_index[lcd_settings_selected] = (uint8_t)(
            (lcd_settings_value_index[lcd_settings_selected] + 1u) %
            lcd_settings_value_count(lcd_settings_selected));
        lcd_settings_apply_current();
        board_display_settings_redraw();
    }
}

void board_display_settings_list_exit(void)
{
    if (!board_display_settings_list_active()) return;
    if (lcd_settings_editing) {
        if (lcd_settings_selected == SETTING_USE_SPI) {
            lcd_settings_spi_failed = false;
        }
        if (lcd_settings_selected == SETTING_RESET) {
            lcd_cleanup_working_pending = false;
            lcd_cleanup_dismiss_success();
        }
        lcd_settings_editing = false;
        board_display_settings_redraw();
        return;
    }
    lcd_settings_list_active = false;
    board_display_settings_redraw();
}

void board_display_set_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction,
                            uint32_t uptime_pulse_count,
                            uint32_t day_pulse_count,
                            uint32_t total_pulse_count,
                            uint32_t measurement_seconds)
{
    static uint32_t previous_dose_thousandths = UINT32_MAX;
    static uint32_t previous_dose_fraction = UINT32_MAX;
    static uint32_t previous_dose_pulse_count = UINT32_MAX;
    static uint32_t previous_measurement_seconds = UINT32_MAX;
    bool period_pulses_changed = false;
    const uint32_t period_pulses[3] = {
        uptime_pulse_count, day_pulse_count, total_pulse_count
    };
    for (uint8_t i = 0u; i < 3u; ++i) {
        if (lcd_dose_period_pulses[i] != period_pulses[i]) {
            period_pulses_changed = true;
        }
        lcd_dose_period_pulses[i] = period_pulses[i];
    }
    const uint8_t selected_period =
        lcd_settings_value_index[SETTING_DOSE_PERIOD] < 3u ?
            lcd_settings_value_index[SETTING_DOSE_PERIOD] : 0u;
    const uint32_t dose_pulse_count = period_pulses[selected_period];
    const bool dose_changed = dose_thousandths != previous_dose_thousandths ||
        dose_fraction != previous_dose_fraction ||
        dose_pulse_count != previous_dose_pulse_count;
    const bool uptime_changed = measurement_seconds !=
        previous_measurement_seconds;

    lcd_dose_value_thousandths = dose_thousandths;
    lcd_dose_fraction = dose_fraction;
    lcd_daily_pulse_count = dose_pulse_count;
    lcd_dose_measurement_seconds = measurement_seconds;
    previous_measurement_seconds = measurement_seconds;
    if (dose_thousandths != 0u || dose_fraction != 0u ||
        (lcd_settings_value_index[SETTING_UNIT] == 1u && dose_pulse_count != 0u)) {
        lcd_dose_has_measurement = true;
    }
    if (dose_changed) {
        previous_dose_thousandths = dose_thousandths;
        previous_dose_fraction = dose_fraction;
        previous_dose_pulse_count = dose_pulse_count;
        if (lcd_screen_page == 1u) {
            lcd_format_dose_display(dose_thousandths, dose_fraction);
            lcd_format_dose_row();
            lcd_cpm_redraw_pending = true;
        }
    } else if (lcd_screen_page == 1u && !lcd_dose_row_hidden &&
               lcd_dose_row_option(lcd_settings_value_index[
                   SETTING_DOSE_ROW_2]) != LCD_DOSE_ROW_OFF &&
               (lcd_dose_row_option(lcd_settings_value_index[
                    SETTING_DOSE_ROW_2]) == LCD_DOSE_ROW_TIME ?
                    uptime_changed : period_pulses_changed)) {
        /* Refresh only row two; repainting the main value makes its black
         * glyph backgrounds visibly flash while elapsed values update. */
        lcd_dose_row_redraw_pending = true;
    }
}

void board_display_set_count(uint32_t count)
{
    if (lcd_count_total == count) return;
    lcd_count_total = count;
    if (lcd_screen_page == 3u) lcd_debug_redraw_pending = true;
    if (lcd_settings_value_index[SETTING_UNIT] == 1u && lcd_screen_page == 1u) {
        if (count != 0u) lcd_dose_has_measurement = true;
        lcd_format_dose_display(lcd_dose_value_thousandths,
                                lcd_dose_fraction);
        lcd_format_dose_row();
        lcd_cpm_redraw_pending = true;
    }
}

void board_display_set_rate(uint32_t rate_thousandths, uint32_t cpm,
                            uint32_t pulses_since_start, bool window_ready)
{
    static uint32_t previous_rate_thousandths = UINT32_MAX;
    static uint32_t previous_cpm = UINT32_MAX;
    static uint32_t previous_warmup_pulses = UINT32_MAX;
    static bool previous_window_ready;
    bool changed = false;
    const uint64_t rate_scaled = (uint64_t)cpm * 666u;
    uint16_t rate_color = LCD_COUNTER_BLUE;

    lcd_rate_value_thousandths = rate_thousandths;
    lcd_warmup_pulses = pulses_since_start;
    if (pulses_since_start != 0u || cpm != 0u) {
        lcd_rate_has_measurement = true;
    }
    lcd_rate_window_ready = window_ready;
    if (pulses_since_start != previous_warmup_pulses ||
        window_ready != previous_window_ready) {
        previous_warmup_pulses = pulses_since_start;
        previous_window_ready = window_ready;
        changed = true;
    }

    if (window_ready) {
        rate_color = LCD_COUNTER_GREEN;
        if (rate_scaled >= 40000u && rate_scaled <= 90000u) {
            rate_color = LCD_COUNTER_YELLOW;
        } else if (rate_scaled > 90000u) {
            rate_color = LCD_COUNTER_RED;
        }
    }
    if (rate_color != lcd_rate_color) {
        lcd_rate_color = rate_color;
        if (rate_color == LCD_COUNTER_RED) {
            lcd_rate_led_mask = 1u;
        } else if (rate_color == LCD_COUNTER_YELLOW) {
            lcd_rate_led_mask = 3u;
        } else if (rate_color == LCD_COUNTER_GREEN) {
            lcd_rate_led_mask = 2u;
        } else if (rate_color == LCD_COUNTER_BLUE) {
            lcd_rate_led_mask = 5u;
        } else {
            lcd_rate_led_mask = 6u;
        }
        changed = true;
    }

    /* The stock UI retains CPM at zero and rate below 0.01 uSv/h. */
    lcd_current_cpm = cpm;
    if (cpm != previous_cpm) {
        previous_cpm = cpm;
        lcd_format_cpm(cpm);
        if (lcd_settings_value_index[SETTING_UNIT] == 1u && lcd_screen_page == 0u) {
            lcd_format_rate_display(rate_thousandths);
        }
        changed = true;
    }
    if (cpm != 0u && rate_thousandths != previous_rate_thousandths) {
        previous_rate_thousandths = rate_thousandths;
        if (lcd_screen_page == 0u) {
            lcd_format_rate_display(rate_thousandths);
        }
        changed = true;
    }
    if (changed && lcd_ready && lcd_screen_page != LCD_GRAPH_PAGE) {
        lcd_cpm_redraw_pending = true;
    }
}

void board_display_history_updated(void)
{
    if (lcd_screen_page == LCD_GRAPH_PAGE && lcd_ready &&
        !lcd_text_active && !lcd_background_pending && !lcd_fill_active &&
        !lcd_dma_active && deadline_reached(lcd_graph_refresh_deadline)) {
        if (lcd_graph_log_type &&
            (lcd_graph_log_window_start != 0u || lcd_graph_log_type == 4u ||
             lcd_graph_cursor_mode || lcd_graph_selected_record != 0u)) {
            lcd_graph_refresh_deadline = board_millis + 15000u;
            return;
        }
        lcd_graph_input_locked = true;
        if (lcd_graph_log_type) {
            lcd_graph_reset_log_window();
            lcd_graph_log_summary_work_active = true;
        } else {
            lcd_cpm_redraw_pending = true;
        }
        lcd_graph_refresh_deadline = board_millis +
            (lcd_graph_log_type == 1u ? 5000u :
             (lcd_graph_log_type ? 15000u : 500u));
    }
}

void board_display_set_debug_usage(uint32_t screen_seconds,
                                  uint32_t sleep_seconds,
                                  uint32_t usb_seconds)
{
    lcd_debug_screen_seconds = screen_seconds;
    lcd_debug_sleep_seconds = sleep_seconds;
    lcd_debug_usb_seconds = usb_seconds;
}

uint8_t board_display_rate_led_mask(void)
{
    return lcd_rate_led_mask;
}

void board_display_set_rate_led(void)
{
    if (lcd_rate_color == LCD_COUNTER_BLUE) {
        /* Use pure blue for the warm-up indication. */
        board_rgb_set_color(0u, 0u, 255u);
    } else {
        board_rgb_set(lcd_rate_led_mask);
    }
}

static uint16_t lcd_battery_percent(uint16_t raw)
{
    const uint16_t battery_empty_adc = 1300u;
    const uint16_t battery_full_adc = 2000u;

    if (raw <= battery_empty_adc) {
        return 0u;
    }
    if (raw >= battery_full_adc) {
        return 100u;
    }
    return (uint16_t)(100u -
        (((uint32_t)(battery_full_adc - raw) * 100u) /
         (battery_full_adc - battery_empty_adc)));
}

static uint16_t lcd_battery_percent_hysteresis(uint16_t percent,
                                                uint16_t raw)
{
    uint16_t next_boundary = (uint16_t)(1301u + percent * 7u);

    while (percent < 100u && raw >= (uint16_t)(next_boundary + 3u)) {
        ++percent;
        next_boundary = (uint16_t)(next_boundary + 7u);
    }
    while (percent != 0u && raw <= (uint16_t)(next_boundary - 10u)) {
        --percent;
        next_boundary = (uint16_t)(next_boundary - 7u);
    }
    return percent;
}

static void lcd_apply_brightness_settings(void)
{
    static const uint8_t levels[] = {
        0u, 1u, 3u, 5u, 10u, 25u, 50u, 80u, 100u
    };
    const uint8_t led_setting = lcd_settings_value_index[SETTING_LED_BRIGHT];
    const uint8_t screen_setting = lcd_settings_value_index[SETTING_SCREEN_BRIGHT];
    uint8_t led_percent;
    uint8_t screen_percent;
    bool apply_led = true;
    bool apply_screen = true;

    if (led_setting == 0u) {
        apply_led = lcd_solar_sample_ready;
        if (lcd_displayed_solar_percent >= 30u) {
            led_percent = 100u;
        } else {
            led_percent = (uint8_t)(5u +
                ((uint32_t)lcd_displayed_solar_percent * 95u) / 30u);
        }
    } else {
        led_percent = levels[led_setting];
    }
    if (screen_setting == 0u) {
        uint32_t raw_sum = 0u;
        uint16_t raw_average = 0u;
        apply_screen = lcd_solar_brightness_sample_count >=
            LCD_SOLAR_BRIGHTNESS_SAMPLE_COUNT;
        for (uint8_t i = 0u;
             i < lcd_solar_brightness_sample_count; ++i) {
            raw_sum += lcd_solar_brightness_samples[i];
        }
        if (lcd_solar_brightness_sample_count != 0u) {
            raw_average = (uint16_t)(raw_sum /
                lcd_solar_brightness_sample_count);
        }
        if (raw_average < 10u) {
            lcd_auto_backlight_target_percent = 1u;
        } else {
            uint16_t solar_percent = (uint16_t)(raw_average / 31u);
            /* Preserve the existing RAW 10 / SOLAR 10% thresholds. */
            if (solar_percent < 10u) solar_percent = 10u;
            if (solar_percent > 100u) solar_percent = 100u;
            lcd_auto_backlight_target_percent = (uint8_t)solar_percent;
        }
        screen_percent = lcd_auto_backlight_target_percent;
    } else {
        screen_percent = levels[screen_setting];
    }
    if (apply_led && rgb_brightness_percent != led_percent) {
        board_rgb_set_brightness(led_percent);
    }
    if (apply_screen && screen_setting != 0u &&
        backlight_brightness_percent != screen_percent) {
        board_lcd_backlight_set_brightness(screen_percent);
    }
}

static void lcd_backlight_auto_ramp_service(void)
{
    if (!backlight_requested_on || lcd_settings_value_index[SETTING_SCREEN_BRIGHT] != 0u ||
        lcd_solar_brightness_sample_count <
            LCD_SOLAR_BRIGHTNESS_SAMPLE_COUNT) {
        lcd_backlight_ramp_last_ms = board_millis;
        return;
    }
    if ((uint32_t)(board_millis - lcd_backlight_ramp_last_ms) <
        LCD_BACKLIGHT_RAMP_INTERVAL_MS) {
        return;
    }

    lcd_backlight_ramp_last_ms = board_millis;
    if (backlight_brightness_percent < lcd_auto_backlight_target_percent) {
        board_lcd_backlight_set_brightness(
            (uint8_t)(backlight_brightness_percent + 1u));
    } else if (backlight_brightness_percent >
               lcd_auto_backlight_target_percent) {
        board_lcd_backlight_set_brightness(
            (uint8_t)(backlight_brightness_percent - 1u));
    }
}

static void lcd_format_percent(uint16_t value, uint8_t offset)
{
    if (value > 999u) {
        value = 999u;
    }

    lcd_adc_glyphs[offset + 0u] = value >= 100u ?
        (char)('0' + (value / 100u)) : ' ';
    lcd_adc_glyphs[offset + 1u] = value >= 10u ?
        (char)('0' + ((value / 10u) % 10u)) : ' ';
    lcd_adc_glyphs[offset + 2u] = (char)('0' + (value % 10u));
    lcd_adc_glyphs[offset + 3u] = '%';
}

void board_display_set_adc(uint16_t channel2, uint16_t channel8,
                           uint16_t channel9)
{
    bool changed = false;
    uint16_t battery_percent;
    uint16_t solar_percent;

    if (channel2 <= 4095u) {
        if (channel2 != lcd_adc_battery_raw) changed = true;
        lcd_adc_battery_raw = channel2;
    }
    if (channel8 <= 4095u) {
        if (channel8 != lcd_adc_usb_raw) changed = true;
        lcd_adc_usb_raw = channel8;
    }
    if (channel9 <= 4095u) {
        if (channel9 != lcd_adc_sun_raw) changed = true;
        lcd_adc_sun_raw = channel9;
    }
    if (channel2 <= 4095u) {
        battery_percent = lcd_battery_percent(channel2);
        if (!lcd_battery_percent_initialized) {
            /* Give the display a useful initial value; debounce later changes. */
            lcd_adc_battery_start_raw = channel2;
            lcd_battery_start_percent = battery_percent;
            lcd_displayed_battery_percent = battery_percent;
            lcd_battery_percent_candidate = battery_percent;
            lcd_battery_percent_candidate_since_ms = board_millis;
            lcd_battery_percent_initialized = true;
            lcd_format_percent(battery_percent, 13u);
            changed = true;
        } else {
            const uint16_t candidate = lcd_battery_percent_hysteresis(
                lcd_battery_percent_candidate, channel2);
            if (candidate != lcd_battery_percent_candidate) {
                lcd_battery_percent_candidate = candidate;
                lcd_battery_percent_candidate_since_ms = board_millis;
            } else if (candidate != lcd_displayed_battery_percent &&
                       (uint32_t)(board_millis -
                           lcd_battery_percent_candidate_since_ms) >=
                               LCD_BATTERY_PERCENT_STABLE_MS) {
                lcd_displayed_battery_percent = candidate;
                lcd_format_percent(candidate, 13u);
                changed = true;
            }
        }
    }
    if (channel9 <= 4095u) {
        if (lcd_solar_brightness_sample_count == 0u) {
            /* Start responsively, then converge to a full 10 s average. */
            for (uint8_t i = 0u;
                 i < LCD_SOLAR_BRIGHTNESS_SAMPLE_COUNT; ++i) {
                lcd_solar_brightness_samples[i] = channel9;
            }
            lcd_solar_brightness_sample_count =
                LCD_SOLAR_BRIGHTNESS_SAMPLE_COUNT;
            lcd_solar_brightness_sample_next = 0u;
        } else {
            lcd_solar_brightness_samples[lcd_solar_brightness_sample_next] =
                channel9;
            lcd_solar_brightness_sample_next = (uint8_t)(
                (lcd_solar_brightness_sample_next + 1u) %
                LCD_SOLAR_BRIGHTNESS_SAMPLE_COUNT);
        }
        solar_percent = (uint16_t)(channel9 / 31u);
        if (solar_percent > 100u) solar_percent = 100u;
        lcd_solar_sample_ready = true;
        if (solar_percent != lcd_displayed_solar_percent) {
            lcd_displayed_solar_percent = solar_percent;
            lcd_format_percent(solar_percent, 7u);
            changed = true;
        }
        lcd_apply_brightness_settings();
    }
    if (changed && lcd_ready) {
        if (lcd_screen_page != LCD_GRAPH_PAGE) {
            lcd_cpm_redraw_pending = true;
        }
        lcd_debug_redraw_pending = true;
    }
}

bool board_display_battery_below_3_percent(void)
{
    return lcd_battery_percent_initialized &&
        (lcd_displayed_battery_percent < 3u ||
         lcd_battery_percent_candidate < 3u);
}

static void lcd_text_render_service(void)
{
    /* Finish in-flight work, but leave new redraws pending while the panel
     * is dark. Rasterizing them on MSI delays the next button response. */
    if (!backlight_requested_on && !lcd_text_active) {
        return;
    }
    if (lcd_background_pending || lcd_fill_start_pending || lcd_fill_active ||
        lcd_dma_active || !lcd_queue_drained()) {
        return;
    }
    if (!lcd_text_active && lcd_graph_partial_pending &&
        lcd_screen_page == LCD_GRAPH_PAGE && lcd_graph_log_type != 0u) {
        lcd_text_active = true;
        lcd_text_glyph_index = 0u;
        lcd_text_row_commands_queued = false;
        lcd_graph_partial_pending = false;
    }
    if (!lcd_text_active && lcd_cpm_redraw_pending) {
        lcd_text_active = true;
        lcd_text_glyph_index = 0u;
        lcd_text_row_commands_queued = false;
        lcd_text_main_rows_only = false;
        lcd_cpm_redraw_pending = false;
        lcd_main_rows_redraw_pending = false;
        lcd_dose_row_redraw_pending = false;
    }
    if (!lcd_text_active && lcd_main_rows_redraw_pending) {
        lcd_text_active = true;
        lcd_text_glyph_index = 0u;
        lcd_text_row_commands_queued = false;
        lcd_text_main_rows_only = true;
        lcd_main_rows_redraw_pending = false;
    }
    if (!lcd_text_active && lcd_dose_row_redraw_pending) {
        lcd_text_active = true;
        lcd_text_glyph_index = (uint8_t)(LCD_RATE_MAX_GLYPHS + 5u);
        lcd_text_row_commands_queued = false;
        lcd_text_main_rows_only = false;
        lcd_dose_row_redraw_pending = false;
    }
    if (!lcd_text_active && lcd_screen_page == 2u &&
        lcd_settings_redraw_pending) {
        lcd_text_active = true;
        lcd_text_glyph_index = 0u;
        lcd_text_row_commands_queued = false;
        lcd_settings_redraw_pending = false;
    }
    if (lcd_screen_page == 3u && lcd_debug_redraw_pending &&
        !lcd_text_active) {
        lcd_text_active = true;
        lcd_text_glyph_index = 0u;
        lcd_text_row_commands_queued = false;
        lcd_debug_redraw_pending = false;
    }
    if (!lcd_text_active) {
        return;
    }
    if (!lcd_text_row_commands_queued) {
        (void)lcd_text_prepare_glyph();
        return;
    }

    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 11u; /* D/C=data */
    lcd_text_stream_offset = 0u;
    lcd_text_stream_total = lcd_text_glyph_pixels;
    lcd_text_stream_advance = lcd_text_dma_glyph_advance;
    lcd_text_stream_next();
}

void board_display_service(void)
{
    if (!lcd_started) {
        return;
    }
    if (!lcd_spi_ready && lcd_ready) {
        /* Recover the transport without losing the selected graph/menu. */
        DMA1_CH5_CCR = 0u;
        DMA1_IFCR = 0xFu << 16u;
        REG32(SPI2_BASE + SPI_CR2) = 0u;
        REG32(SPI2_BASE + SPI_CR1) = 0u;
        lcd_queue_head = 0u;
        lcd_queue_tail = 0u;
        lcd_dma_active = false;
        lcd_dma_is_text_row = false;
        lcd_fill_active = false;
        lcd_fill_start_pending = false;
        lcd_text_active = false;
        lcd_text_row_commands_queued = false;
        lcd_spi_error = false;
        lcd_ready = false;
        lcd_background_pending = true;
        lcd_cpm_redraw_pending = true;
        lcd_configure();
    }
    lcd_backlight_auto_ramp_service();
    lcd_spi_pump();
    if (lcd_init_state == LCD_INIT_FAILED || !lcd_spi_ready) {
        return;
    }
    if (lcd_cleanup_status == 1u &&
        deadline_reached(lcd_cleanup_status_deadline)) {
        lcd_cleanup_dismiss_success();
        board_display_settings_redraw();
    }
    switch (lcd_init_state) {
    case LCD_INIT_BOARD_ENABLE_DELAY:
        if (deadline_reached(lcd_init_deadline)) {
            /* Stock LCD_Init asserts CS only after the PB7 settle delay. */
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 8u);
            lcd_init_deadline = board_millis + 5u;
            lcd_init_state = LCD_INIT_RESET_LOW;
        }
        return;
    case LCD_INIT_RESET_LOW:
        if (deadline_reached(lcd_init_deadline)) {
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 8u;
            lcd_init_deadline = board_millis + 5u;
            lcd_init_state = LCD_INIT_RESET_HIGH;
        }
        return;
    case LCD_INIT_RESET_HIGH:
        if (deadline_reached(lcd_init_deadline)) {
#if LCD_DIAGNOSTIC_COMMANDS == 0u
            lcd_ready = true;
            lcd_init_state = LCD_INIT_READY;
#else
            /* Stock sequence starts with SWRESET after the hardware reset. */
            lcd_command(0x01u, 0, 0u);
            lcd_init_state = LCD_INIT_SWRESET_TX;
#endif
        }
        return;
    case LCD_INIT_SWRESET_TX:
        if (lcd_queue_drained()) {
            lcd_init_deadline = board_millis + 150u;
            lcd_init_state = LCD_INIT_SWRESET_DELAY;
        }
        return;
    case LCD_INIT_SWRESET_DELAY:
        if (deadline_reached(lcd_init_deadline)) {
            lcd_command(0x11u, 0, 0u); /* SLPOUT */
            lcd_init_state = LCD_INIT_SLPOUT_TX;
        }
        return;
    case LCD_INIT_SLPOUT_TX:
        if (lcd_queue_drained()) {
            /* Match the stock table's 500 ms sleep-exit delay. */
            lcd_init_deadline = board_millis + 500u;
            lcd_init_state = LCD_INIT_SLPOUT_DELAY;
        }
        return;
    case LCD_INIT_SLPOUT_DELAY:
        if (deadline_reached(lcd_init_deadline)) {
            lcd_send_controller_config();
            if (!lcd_spi_ready) {
                lcd_init_state = LCD_INIT_FAILED;
                return;
            }
            lcd_init_state = LCD_INIT_CONFIG_TX;
        }
        return;
    case LCD_INIT_CONFIG_TX:
        if (lcd_queue_drained()) {
            lcd_init_deadline = board_millis + 10u;
            lcd_init_state = LCD_INIT_NORON_DELAY;
        }
        return;
    case LCD_INIT_NORON_DELAY:
        if (deadline_reached(lcd_init_deadline)) {
            lcd_command(0x13u, 0, 0u); /* NORON */
            lcd_init_state = LCD_INIT_NORON_TX;
        }
        return;
    case LCD_INIT_NORON_TX:
        if (lcd_queue_drained()) {
            lcd_init_deadline = board_millis + 10u;
            lcd_init_state = LCD_INIT_DISPON_DELAY;
        }
        return;
    case LCD_INIT_DISPON_DELAY:
        if (deadline_reached(lcd_init_deadline)) {
            lcd_command(0x29u, 0, 0u); /* DISPON */
            lcd_init_state = LCD_INIT_DISPON_TX;
        }
        return;
    case LCD_INIT_DISPON_TX:
        if (lcd_queue_drained()) {
            lcd_init_deadline = board_millis + 100u;
            lcd_init_state = LCD_INIT_FINAL_DELAY;
        }
        return;
    case LCD_INIT_FINAL_DELAY:
        if (deadline_reached(lcd_init_deadline)) {
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u; /* CS high after init */
            /* Stock main() sends INVOFF as a separate CS transaction. */
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x20u, 0, 0u);
            lcd_init_state = LCD_INIT_POST_INIT_INVOFF_TX;
        }
        break;
    case LCD_INIT_POST_INIT_INVOFF_TX:
        if (lcd_queue_drained()) {
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u; /* CS high */
            lcd_ready = true;
            lcd_init_state = LCD_INIT_READY;
            /* Clear the panel before drawing the first screen. */
            lcd_background_pending = true;
        }
        break;
    case LCD_INIT_READY:
        break;
    default:
        return;
    }

    if (!lcd_ready || !lcd_spi_ready) {
        return;
    }
    /* Never interrupt an active DMA transfer. At the next chunk boundary,
     * discard the stale frame and render the most recent user action. */
    if (lcd_graph_pending_action != 0 && !lcd_graph_loading &&
        !lcd_graph_log_summary_work_active &&
        lcd_screen_page == LCD_GRAPH_PAGE && !lcd_dma_active &&
        lcd_queue_drained() && !lcd_fill_active &&
        !lcd_background_pending) {
        const int8_t action = lcd_graph_pending_action;
        lcd_graph_pending_action = 0;
        lcd_text_active = false;
        lcd_text_row_commands_queued = false;
        lcd_cpm_redraw_pending = false;
        lcd_graph_input_locked = false;
        if (action == LCD_GRAPH_PENDING_LEFT) {
            board_display_left_short();
        } else if (action == LCD_GRAPH_PENDING_RIGHT) {
            board_display_right_short();
        } else if (action == LCD_GRAPH_PENDING_TOGGLE) {
            (void)board_display_graph_long_press();
        } else if (board_display_graph_back()) {
            /* Back to range selection / cursor browsing. */
        } else {
            board_display_extra_exit();
        }
    }
    if (lcd_screen_page < 2u) {
        rtc_calendar_t calendar;
        uint32_t minute_key = board_uptime_seconds / 60u;
        if (rtc_time_valid && rtc_read_calendar(&calendar)) {
            minute_key = (uint32_t)calendar.year * 1000000u +
                (uint32_t)calendar.month * 10000u +
                (uint32_t)calendar.day * 100u +
                (uint32_t)calendar.hour * 60u + calendar.minute;
        }
        if (minute_key != lcd_displayed_minute_key) {
            lcd_displayed_minute_key = minute_key;
            if (lcd_screen_page == 1u &&
                lcd_settings_value_index[SETTING_DOSE_PERIOD] == 0u) {
                lcd_main_rows_redraw_pending = true;
            } else {
                lcd_cpm_redraw_pending = true;
            }
        }
    } else if (lcd_screen_page == 3u) {
        static uint32_t debug_displayed_second = UINT32_MAX;
        if (board_uptime_seconds != debug_displayed_second) {
            debug_displayed_second = board_uptime_seconds;
            lcd_debug_redraw_pending = true;
        }
    }
    if (lcd_background_pending && lcd_queue_drained() && !lcd_fill_active &&
        !lcd_dma_active) {
        lcd_background_pending = false;
        lcd_begin_fill();
    }
    lcd_text_render_service();
    if (lcd_screen_page == LCD_GRAPH_PAGE && lcd_graph_log_type != 0u &&
        lcd_graph_loading && !lcd_graph_input_locked &&
        !lcd_graph_log_summary_work_active) {
        /* The WAIT frame has completed. Start bounded preparation now. */
        lcd_graph_log_summary_work_active = true;
        lcd_graph_input_locked = true;
    }
    if (lcd_screen_page == LCD_GRAPH_PAGE &&
        lcd_graph_log_summary_work_active && !rgb_flash_active &&
        !click_sound_active) {
        if (lcd_graph_update_log_summary()) {
            lcd_graph_log_summary_work_active = false;
            lcd_graph_loading = false;
            lcd_graph_input_locked = true;
            lcd_cpm_redraw_pending = true;
        }
    }
    if (lcd_cleanup_working_pending && lcd_ready &&
        !lcd_text_active && !lcd_settings_redraw_pending &&
        lcd_queue_drained() && !lcd_fill_active && !lcd_dma_active) {
        lcd_cleanup_working_pending = false;
        lcd_cleanup_status = lcd_cleanup_execute() ? 1u : 2u;
        if (lcd_cleanup_status == 1u) {
            lcd_cleanup_status_deadline = board_millis + 3000u;
        }
        board_display_settings_redraw();
    }
}

bool board_display_busy(void)
{
    if (!lcd_started) {
        return false;
    }
    if (lcd_spi_error) {
        return false;
    }
    const bool redraw_pending = backlight_requested_on &&
        (lcd_cpm_redraw_pending ||
         lcd_graph_partial_pending ||
         (lcd_screen_page == LCD_GRAPH_PAGE &&
          deadline_reached(lcd_graph_refresh_deadline)) ||
         lcd_dose_row_redraw_pending || lcd_main_rows_redraw_pending ||
         (lcd_screen_page == 2u && lcd_settings_redraw_pending) ||
         lcd_cleanup_working_pending ||
         (lcd_screen_page == 3u && lcd_debug_redraw_pending));
    return !lcd_ready || !lcd_queue_drained() || lcd_background_pending ||
           lcd_fill_start_pending ||
           lcd_fill_active || lcd_text_active || redraw_pending ||
           (lcd_screen_page == LCD_GRAPH_PAGE &&
            (lcd_graph_loading || lcd_graph_log_summary_work_active));
}

void board_display_init(void)
{
    if (LCD_SPI_ENABLED == 0u || lcd_started) {
        return;
    }
    lcd_screen_page = 0u;
    lcd_main_return_page = 0u;
    lcd_settings_list_active = false;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    board_rtc_init();
    settings_eeprom_load();
    settings_row_load();
    settings_spi_load();
    settings_clicks_load();
    settings_long_press_load();
    settings_click_tone_load();
    settings_sound_alarm_load();
    settings_alarm_stop_load();
    settings_timer_load();
    settings_alarm_load();
    settings_hv_load();
    settings_light_alarm_load();
    settings_dose_alarm_load();
    settings_rate_custom_load();
    settings_dose_actions_load();
    settings_visibility_load();
    lcd_screen_page = 0u;
    lcd_last_extra_page = 2u;
    lcd_main_return_page = 0u;
    lcd_settings_selected = lcd_settings_visible_to_item(0u);
    if (settings_log_source == BOARD_LOG_MEMORY_SPI) {
        (void)spi_nor_init();
    }
    lcd_apply_brightness_settings();
    /* Replace static startup labels with the loaded unit/prefix choices
     * before the LCD's initial background fill and first text render. */
    lcd_format_dose(lcd_dose_value_thousandths, lcd_dose_fraction);
    lcd_format_rate(lcd_rate_value_thousandths);
    lcd_blank_large_value();
    lcd_cpm_redraw_pending = true;
    lcd_started = true;
    lcd_configure();
    /* RTC, saved configuration and display are ready before enabling HV. */
    board_hv_set_enabled(lcd_settings_value_index[SETTING_HV] == 0u);
}

static uint32_t gpio_read(uint32_t base, uint32_t pin)
{
    return (REG32(base + GPIO_IDR) >> pin) & 1u;
}

static void pin_input_with_pull(uint32_t base, uint32_t pin)
{
    volatile uint32_t *moder = (volatile uint32_t *)(uintptr_t)(base + GPIO_MODER);
    volatile uint32_t *pupdr = (volatile uint32_t *)(uintptr_t)(base + GPIO_PUPDR);
    uint32_t shift = pin * 2u;
    uint32_t pull = BOARD_BUTTON_ACTIVE_LOW ? 1u : 2u;

    *moder &= ~(3u << shift);
    *pupdr = (*pupdr & ~(3u << shift)) | (pull << shift);
}

static void detector_input_init(void)
{
    volatile uint32_t *exticr2 =
        (volatile uint32_t *)(uintptr_t)(SYSCFG_BASE + 0x0Cu);
    volatile uint32_t *exticr3 =
        (volatile uint32_t *)(uintptr_t)(SYSCFG_BASE + 0x10u);
    const uint32_t exti_port_shift = (DETECTOR_PIN - 4u) * 4u;
    const uint32_t button_exti_mask =
        (1u << BOARD_BUTTON_RIGHT_PIN) | (1u << BOARD_BUTTON_LEFT_PIN);

    /* Stock HEX dispatches EXTI lines 4, 6 and 9. Lines 4/9 are the known
     * buttons; PA6 is the confirmed blue LED output, so PB6 is the remaining
     * detector-input candidate. Route PB6 to EXTI6 and count its falling edge. */
    pin_input_with_pull(DETECTOR_GPIO_BASE, DETECTOR_PIN);
    RCC_APB2ENR |= 1u; /* SYSCFG clock */
    (void)RCC_APB2ENR;
    *exticr2 = (*exticr2 & ~(0xFu << exti_port_shift)) |
        (1u << exti_port_shift); /* EXTI6 source = GPIOB */
    *exticr2 &= ~0xFu; /* EXTI4 source = GPIOA (right button). */
    *exticr3 = (*exticr3 & ~(0xFu << 4u)) | (1u << 4u);
    /* EXTI9 source = GPIOB (left button). */
    EXTI_IMR &= ~DETECTOR_EXTI_MASK;
    EXTI_RTSR &= ~DETECTOR_EXTI_MASK;
    EXTI_FTSR |= DETECTOR_EXTI_MASK;
    EXTI_PR = DETECTOR_EXTI_MASK;
    /* Wake on both press and release edges; this also catches very short taps. */
    EXTI_RTSR |= button_exti_mask;
    EXTI_FTSR |= button_exti_mask;
    EXTI_PR = button_exti_mask;
    NVIC_ISER = 1u << 7u; /* EXTI4_15_IRQn */
    EXTI_IMR |= DETECTOR_EXTI_MASK | button_exti_mask;
}

uint32_t board_detector_count(void)
{
    return detector_pulse_count;
}

void board_hv_set_enabled(bool enabled)
{
    if (enabled) {
        REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 5u;
        if (!board_hv_enabled) {
            detector_pulse_count = 0u;
        }
        EXTI_PR = DETECTOR_EXTI_MASK;
        board_hv_enabled = true;
    } else {
        board_hv_enabled = false;
        REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 5u);
        detector_pulse_count = 0u;
        EXTI_PR = DETECTOR_EXTI_MASK;
    }
}

bool board_hv_is_enabled(void)
{
    return board_hv_enabled;
}

void board_adc_init(void)
{
    uint32_t timeout;

    /* These are the three analog inputs configured by the stock firmware:
     * ADC channel 2 = PA2, channel 8 = PB0, and channel 9 = PB1. */
    REG32(GPIOA_BASE + GPIO_MODER) |= 3u << 4u;
    REG32(GPIOA_BASE + GPIO_PUPDR) &= ~(3u << 4u);
    REG32(GPIOB_BASE + GPIO_MODER) |= (3u << 0u) | (3u << 2u);
    REG32(GPIOB_BASE + GPIO_PUPDR) &= ~((3u << 0u) | (3u << 2u));

    /* Stock raises PB10 for 5 ms before measuring the analog channels. */
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + 10u);
    REG32(GPIOB_BASE + GPIO_OTYPER) &= ~(1u << 10u);
    REG32(GPIOB_BASE + GPIO_PUPDR) &= ~(3u << 20u);
    REG32(GPIOB_BASE + GPIO_MODER) =
        (REG32(GPIOB_BASE + GPIO_MODER) & ~(3u << 20u)) | (1u << 20u);

    RCC_APB2ENR |= 1u << 9u; /* ADC1 clock */
    (void)RCC_APB2ENR;
    ADC_CR = 0u;
    ADC_CFGR1 = 0u; /* Single conversion, software trigger, 12-bit result. */
    ADC_CFGR2 = 1u << 30u; /* Keep the previously working PCLK/4 ADC clock. */
    ADC_SMPR = 7u; /* Longest sample time for high source impedance. */

    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 10u;
    adc_wait_ms(5u);
    ADC_CR = 1u << 31u; /* Calibrate before enabling. */
    timeout = 1000000u;
    while ((ADC_CR & (1u << 31u)) != 0u && --timeout != 0u) { }
    if (timeout == 0u) {
        REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + 10u);
        return;
    }

    adc_calibrated = true;
    /* Calibration is retained with ADEN=0; remove regulator and clock load. */
    adc_disable();
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + 10u);
}

static bool adc_enable(void)
{
    uint32_t timeout = 1000000u;

    if (!adc_calibrated) {
        return false;
    }

    RCC_APB2ENR |= 1u << 9u;
    (void)RCC_APB2ENR;
    ADC_ISR = 1u; /* Clear ADRDY before enabling. */
    ADC_CR = 1u; /* ADEN; the ADC regulator starts with the converter. */
    while ((ADC_ISR & 1u) == 0u && --timeout != 0u) { }
    adc_ready = timeout != 0u;
    return adc_ready;
}

static void adc_disable(void)
{
    uint32_t timeout;

    if ((ADC_CR & (1u << 2u)) != 0u) {
        ADC_CR |= 1u << 4u; /* ADSTP */
        timeout = 1000000u;
        while ((ADC_CR & (1u << 4u)) != 0u && --timeout != 0u) { }
        if (timeout == 0u) {
            adc_ready = false;
            return;
        }
    }

    if ((ADC_CR & 1u) != 0u) {
        ADC_CR |= 1u << 1u; /* ADDIS */
        timeout = 1000000u;
        while ((ADC_CR & 1u) != 0u && --timeout != 0u) { }
        if (timeout == 0u) {
            adc_ready = false;
            return;
        }
    }

    /* Preserve calibration while removing the internal ADC regulator load. */
    ADC_CR &= ~(1u << 28u); /* ADVREGEN */
    adc_ready = false;
    RCC_APB2ENR &= ~(1u << 9u);
}

uint16_t board_adc_read(uint8_t channel)
{
    uint32_t timeout;

    if (!adc_ready || channel > 18u) {
        return UINT16_MAX;
    }

    ADC_CHSELR = 1u << channel;
    ADC_ISR = 1u << 2u; /* Clear EOC. */
    ADC_CR |= 1u << 2u; /* ADSTART */
    timeout = 1000000u;
    while ((ADC_ISR & (1u << 2u)) == 0u && --timeout != 0u) { }
    if (timeout == 0u) {
        return UINT16_MAX;
    }
    return (uint16_t)(ADC_DR & 0x0FFFu);
}

static void adc_wait_ms(uint32_t duration)
{
    uint32_t deadline = board_millis + duration;

    while ((int32_t)(board_millis - deadline) < 0) { }
}

void board_adc_read_selected(bool read_battery, bool read_usb, bool read_solar,
                             uint16_t *channel2, uint16_t *channel8,
                             uint16_t *channel9)
{
    uint16_t values[3] = { UINT16_MAX, UINT16_MAX, UINT16_MAX };

    if (!read_battery && !read_usb && !read_solar) {
        *channel2 = values[0];
        *channel8 = values[1];
        *channel9 = values[2];
        return;
    }

    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 10u;
    if (adc_enable()) {
        adc_wait_ms(5u);
        if (read_battery) values[0] = board_adc_read(2u);
        if (read_usb) values[1] = board_adc_read(8u);
        if (read_solar) values[2] = board_adc_read(9u);
    }
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + 10u);
    adc_disable();
    adc_wait_ms(1u);

    *channel2 = values[0];
    *channel8 = values[1];
    *channel9 = values[2];
}

void EXTI4_15_IRQHandler(void)
{
    const uint32_t pending = EXTI_PR;
    const uint32_t button_mask = (1u << BOARD_BUTTON_LEFT_PIN) |
        (1u << BOARD_BUTTON_RIGHT_PIN);

    if ((pending & DETECTOR_EXTI_MASK) != 0u) {
        EXTI_PR = DETECTOR_EXTI_MASK;
        if (board_hv_enabled && detector_pulse_count < UINT32_MAX) {
            ++detector_pulse_count;
        }
    }
    if ((pending & (1u << BOARD_BUTTON_LEFT_PIN)) != 0u) {
        EXTI_PR = 1u << BOARD_BUTTON_LEFT_PIN;
        power_off_wakeup = true;
    }
    if ((pending & button_mask) != 0u) {
        EXTI_PR = pending & button_mask;
        if ((pending & (1u << BOARD_BUTTON_LEFT_PIN)) != 0u) {
            button_wakeup_pending_mask |= BOARD_BUTTON_LEFT_DOWN;
        }
        if ((pending & (1u << BOARD_BUTTON_RIGHT_PIN)) != 0u) {
            button_wakeup_pending_mask |= BOARD_BUTTON_RIGHT_DOWN;
        }
    }
}

static void board_reset(void)
{
    __asm volatile ("dsb");
    SCB_AIRCR = 0x05FA0004u;
    __asm volatile ("dsb\n\tisb");
    for (;;) {
        __asm volatile ("wfi");
    }
}

void board_reboot(void)
{
    board_reset();
}

void board_power_off(void)
{
    volatile uint32_t *exticr3 =
        (volatile uint32_t *)(uintptr_t)(SYSCFG_BASE + 0x10u);
    const uint32_t button_mask = 1u << BOARD_BUTTON_LEFT_PIN;
    const uint32_t exti_port_shift =
        (BOARD_BUTTON_LEFT_PIN - 8u) * 4u;

    board_lcd_backlight_set(false);
    board_rgb_set(0u);
    while (button_pressed(GPIOB_BASE, BOARD_BUTTON_LEFT_PIN)) {
        /* Ignore a button press already held when power-off was requested. */
    }

    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 5u); /* PA5 low */
    EXTI_IMR &= ~DETECTOR_EXTI_MASK;
    RCC_APB2ENR |= 1u; /* SYSCFG clock */
    (void)RCC_APB2ENR;
    *exticr3 = (*exticr3 & ~(0xFu << exti_port_shift)) |
        (1u << exti_port_shift); /* PB9 -> EXTI9 */
    EXTI_RTSR &= ~button_mask;
    EXTI_FTSR |= button_mask;
    EXTI_PR = button_mask;
    EXTI_IMR |= button_mask;
    NVIC_ISER = 1u << 7u; /* EXTI4_15_IRQn */
    power_off_wakeup = false;
    SYST_CSR = 0u;
    SCB_SCR |= 1u << 2u; /* SLEEPDEEP */

    while (!power_off_wakeup) {
        __asm volatile ("dsb\n\twfi\n\tisb");
    }

    SCB_SCR &= ~(1u << 2u);
    /* Already saved before sleep; SysTick is stopped here, so a second NOR
     * wait would have no timeout clock. */
    board_reset();
}

static bool button_pressed(uint32_t base, uint32_t pin)
{
    uint32_t level = gpio_read(base, pin);
    return BOARD_BUTTON_ACTIVE_LOW ? (level == 0u) : (level != 0u);
}

static void timer_pwm_init(uint32_t base, uint8_t channels)
{
    REG32(base + TIM_PSC) = 0u;
    REG32(base + TIM_ARR) = PWM_PERIOD - 1u;
    REG32(base + TIM_CCMR1) = 0x6868u; /* CH1/2 PWM mode 1 + preload */
    REG32(base + TIM_CCR1) = 0u;
    if ((channels & 2u) != 0u) {
        REG32(base + TIM_CCR2) = 0u;
    }
    REG32(base + TIM_CCER) = ((channels & 1u) ? (1u | (1u << 1)) : 0u) |
                             ((channels & 2u) ? (1u << 4) | (1u << 5) : 0u);
    REG32(base + TIM_EGR) = 1u;
    REG32(base + TIM_CR1) = 1u << 7; /* ARPE; board_rgb_apply starts as needed. */
}

void board_rgb_set_brightness(uint8_t percent)
{
    if (percent > 100u) {
        percent = 100u;
    }
    rgb_brightness_percent = percent;
    board_rgb_apply();
}

void board_init(void)
{
    const uint32_t led_mask = (1u << BOARD_RGB_R_PIN) |
                              (1u << BOARD_RGB_G_PIN) |
                              (1u << BOARD_RGB_B_PIN);
    volatile uint32_t *gpioa_moder = (volatile uint32_t *)(uintptr_t)(GPIOA_BASE + GPIO_MODER);

    /* Configure one Flash wait state before switching to the 32 MHz PLL. */
    FLASH_ACR = (FLASH_ACR & ~1u) | 1u;

    /* Start HSI16 and select it temporarily as SYSCLK before configuring PLL. */
    RCC_CR |= 1u; /* HSI16ON */
    while ((RCC_CR & (1u << 2)) == 0u) { } /* HSI16RDY */
    RCC_CFGR = (RCC_CFGR & ~3u) | 1u;
    while ((RCC_CFGR & (3u << 2)) != (1u << 2)) { }

    /* Match stock: HSI16 x4 /2 = 32 MHz; run AHB/APB at DIV1. */
    RCC_CR &= ~(1u << 24u); /* PLLON=0 before changing PLL factors */
    while ((RCC_CR & (1u << 25u)) != 0u) { } /* PLLRDY=0 */
    RCC_CFGR = (RCC_CFGR & ~((3u << 0u) | (0xFu << 4u) |
        (7u << 8u) | (7u << 11u) | (1u << 16u) |
        (0xFu << 18u) | (3u << 22u))) |
        (1u << 0u) | (1u << 18u) | (1u << 22u);
    RCC_CR |= 1u << 24u; /* PLLON */
    while ((RCC_CR & (1u << 25u)) == 0u) { } /* PLLRDY */
    RCC_CFGR = (RCC_CFGR & ~3u) | 3u; /* SYSCLK=PLL */
    while ((RCC_CFGR & (3u << 2u)) != (3u << 2u)) { }

    RCC_AHBENR |= 1u; /* DMA1 clock */
    RCC_IOPENR |= 3u; /* GPIOA and GPIOB clocks */
    (void)RCC_IOPENR;
    protocol_v1_transport_init();

    /* PB8 controls the flashlight; preload OFF before enabling output mode. */
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + FLASHLIGHT_PIN);
    REG32(GPIOB_BASE + GPIO_OTYPER) &= ~(1u << FLASHLIGHT_PIN);
    REG32(GPIOB_BASE + GPIO_OSPEEDR) &= ~(3u << (FLASHLIGHT_PIN * 2u));
    REG32(GPIOB_BASE + GPIO_PUPDR) &= ~(3u << (FLASHLIGHT_PIN * 2u));
    REG32(GPIOB_BASE + GPIO_MODER) =
        (REG32(GPIOB_BASE + GPIO_MODER) &
         ~(3u << (FLASHLIGHT_PIN * 2u))) |
        (1u << (FLASHLIGHT_PIN * 2u));
    flashlight_enabled = false;

    /* Keep the detector stage off during startup transients. */
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 5u);
    board_hv_enabled = false;
    *gpioa_moder = (*gpioa_moder & ~(3u << 10u)) | (1u << 10u);

    /* Preload the assumed inactive LED level before enabling output mode. */
    REG32(GPIOA_BASE + GPIO_BSRR) = BOARD_RGB_ACTIVE_HIGH ? (led_mask << 16) : led_mask;

    RCC_APB1ENR |= 1u | (1u << 31u); /* TIM2 and LPTIM1 clocks */
    RCC_CCIPR &= ~(3u << 18u); /* LPTIM1 kernel clock = PCLK1 */
    RCC_APB2ENR |= (1u << 5); /* TIM22 clock */
    (void)RCC_APB2ENR;
    timer_pwm_init(TIM2_BASE, 3u);
    timer_pwm_init(TIM22_BASE, 1u);

    /* PA0/PA1 = TIM2_CH1/2 AF2; PA6 = TIM22_CH1 AF5. */
    REG32(GPIOA_BASE + GPIO_AFRL) = (REG32(GPIOA_BASE + GPIO_AFRL) &
        ~((0xFu << 0u) | (0xFu << 4u) | (0xFu << 24u))) |
        (2u << 0u) | (2u << 4u) | (5u << 24u);
    *gpioa_moder = (*gpioa_moder &
        ~((3u << 0u) | (3u << 2u) | (3u << 12u))) |
        (2u << 0u) | (2u << 2u) | (2u << 12u);

    pin_input_with_pull(GPIOB_BASE, BOARD_BUTTON_LEFT_PIN);
    pin_input_with_pull(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN);
    detector_input_init();

    /* Start with LCD backlight disabled; PB2 switches to LPTIM1 PWM on wake. */
    REG32(GPIOB_BASE + GPIO_BSRR) =
        1u << (16u + BOARD_LCD_BACKLIGHT_PIN);
    REG32(GPIOB_BASE + GPIO_MODER) = (REG32(GPIOB_BASE + GPIO_MODER) &
        ~(3u << (BOARD_LCD_BACKLIGHT_PIN * 2u))) |
        (1u << (BOARD_LCD_BACKLIGHT_PIN * 2u));

    button_input_init(&left_button,
        button_pressed(GPIOB_BASE, BOARD_BUTTON_LEFT_PIN), board_millis);
    button_input_init(&right_button,
        button_pressed(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN), board_millis);

    SYST_RVR = 32000u - 1u; /* 1 ms at 32 MHz SYSCLK */
    SYST_CVR = 0u;
    SYST_CSR = 7u; /* processor clock, interrupt, enable */
    systick_period_ms = 1u;
}

void board_set_idle_tick(bool idle)
{
    const uint8_t period_ms = idle ? 10u : 1u;
    uint32_t primask;

    if (systick_period_ms == period_ms) {
        return;
    }

    __asm volatile ("mrs %0, primask\n\tcpsid i" : "=r" (primask) :: "memory");
    SYST_RVR = (((idle_clock_active ? 131072u : 32000000u) *
        period_ms) / 1000u) - 1u;
    SYST_CVR = 0u;
    systick_period_ms = period_ms;
    if ((primask & 1u) == 0u) {
        __asm volatile ("cpsie i" ::: "memory");
    }
}

bool board_set_idle_clock(bool idle)
{
    uint32_t primask;
    uint32_t timeout;

    if (idle_clock_active == idle) return true;
    __asm volatile ("mrs %0, primask\n\tcpsid i" : "=r" (primask) :: "memory");
    SYST_CSR = 0u;
    SCB_ICSR = 1u << 25u; /* Clear pending SysTick across the clock switch. */

    if (idle) {
        /* MSI range 1 is 131.072 kHz on STM32L051. */
        RCC_ICSCR = (RCC_ICSCR & ~(7u << 13u)) | (1u << 13u);
        RCC_CR |= 1u << 8u; /* MSION */
        timeout = 2000000u;
        while ((RCC_CR & (1u << 9u)) == 0u && --timeout != 0u) { }
        if (timeout == 0u) goto clock_change_failed;
        RCC_CFGR &= ~3u; /* SYSCLK = MSI */
        timeout = 2000000u;
        while ((RCC_CFGR & (3u << 2u)) != 0u && --timeout != 0u) { }
        if (timeout == 0u) goto clock_change_failed;
        idle_clock_active = true;
        RCC_CR &= ~(1u << 24u); /* PLL is no longer needed. */
        timeout = 2000000u;
        while ((RCC_CR & (1u << 25u)) != 0u && --timeout != 0u) { }
        RCC_CR &= ~1u; /* HSI16 is no longer needed. */
    } else {
        RCC_CR |= 1u; /* HSI16 */
        timeout = 2000000u;
        while ((RCC_CR & (1u << 2u)) == 0u && --timeout != 0u) { }
        if (timeout == 0u) goto clock_change_failed;
        RCC_CR |= 1u << 24u; /* Restart the configured HSI16 x4 /2 PLL. */
        timeout = 2000000u;
        while ((RCC_CR & (1u << 25u)) == 0u && --timeout != 0u) { }
        if (timeout == 0u) goto clock_change_failed;
        RCC_CFGR = (RCC_CFGR & ~3u) | 3u; /* SYSCLK = PLL */
        timeout = 2000000u;
        while ((RCC_CFGR & (3u << 2u)) != (3u << 2u) && --timeout != 0u) { }
        if (timeout == 0u) goto clock_change_failed;
        idle_clock_active = false;
    }

    SYST_RVR = (((idle_clock_active ? 131072u : 32000000u) *
        systick_period_ms) / 1000u) - 1u;
    if (click_sound_active) board_sound_timer_config();
    SYST_CVR = 0u;
    SYST_CSR = 7u;
    if ((primask & 1u) == 0u) __asm volatile ("cpsie i" ::: "memory");
    return true;

clock_change_failed:
    SYST_RVR = (((idle_clock_active ? 131072u : 32000000u) *
        systick_period_ms) / 1000u) - 1u;
    SYST_CVR = 0u;
    SYST_CSR = 7u;
    if ((primask & 1u) == 0u) __asm volatile ("cpsie i" ::: "memory");
    return false;
}

uint8_t board_buttons_sample_10ms(void)
{
    const uint32_t now = board_millis;
    const uint16_t long_press_ms = settings_long_press_ms();
    button_input_set_long_press_ms(&left_button, long_press_ms);
    button_input_set_long_press_ms(&right_button, long_press_ms);
    const uint8_t left = button_input_update(&left_button,
        button_pressed(GPIOB_BASE, BOARD_BUTTON_LEFT_PIN), now);
    const uint8_t right = button_input_update(&right_button,
        button_pressed(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN), now);
    uint8_t events = 0u;

    if ((left & BUTTON_INPUT_DOWN) != 0u) events |= BOARD_BUTTON_LEFT_DOWN;
    if ((left & BUTTON_INPUT_UP) != 0u) events |= BOARD_BUTTON_LEFT_UP;
    if ((left & BUTTON_INPUT_CLICK) != 0u) events |= BOARD_BUTTON_LEFT;
    if ((left & BUTTON_INPUT_LONG) != 0u) events |= BOARD_BUTTON_LEFT_LONG;
    if ((right & BUTTON_INPUT_DOWN) != 0u) events |= BOARD_BUTTON_RIGHT_DOWN;
    if ((right & BUTTON_INPUT_UP) != 0u) events |= BOARD_BUTTON_RIGHT_UP;
    if ((right & BUTTON_INPUT_CLICK) != 0u) events |= BOARD_BUTTON_RIGHT;
    if ((right & BUTTON_INPUT_LONG) != 0u) events |= BOARD_BUTTON_RIGHT_LONG;
    return events;
}

void board_buttons_suppress_until_release(uint8_t mask)
{
    const uint32_t now = board_millis;
    if ((mask & BOARD_BUTTON_LEFT_DOWN) != 0u) {
        button_input_suppress(&left_button,
            button_pressed(GPIOB_BASE, BOARD_BUTTON_LEFT_PIN), now);
    }
    if ((mask & BOARD_BUTTON_RIGHT_DOWN) != 0u) {
        button_input_suppress(&right_button,
            button_pressed(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN), now);
    }
}

uint8_t board_buttons_pressed_mask(void)
{
    uint8_t pressed = 0u;
    if (button_pressed(GPIOB_BASE, BOARD_BUTTON_LEFT_PIN)) {
        pressed |= BOARD_BUTTON_LEFT_DOWN;
    }
    if (button_pressed(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN)) {
        pressed |= BOARD_BUTTON_RIGHT_DOWN;
    }
    return pressed;
}

uint8_t board_take_button_wakeup_request(void)
{
    uint32_t primask;
    uint8_t requested;
    __asm volatile ("mrs %0, primask\n\tcpsid i" : "=r" (primask) :: "memory");
    requested = button_wakeup_pending_mask;
    button_wakeup_pending_mask = 0u;
    if ((primask & 1u) == 0u) __asm volatile ("cpsie i" ::: "memory");
    return requested;
}

void board_rgb_set(uint8_t logical_rgb_mask)
{
    rgb_channels = logical_rgb_mask & 7u;
    if (rgb_channels == 0u) rgb_flash_active = false;
    rgb_level[0] = (rgb_channels & 1u) ? 255u : 0u;
    rgb_level[1] = (rgb_channels & 2u) ? 255u : 0u;
    rgb_level[2] = (rgb_channels & 4u) ? 255u : 0u;
    board_rgb_apply();
}

uint8_t board_rgb_begin_flash(void)
{
    static const uint8_t duration_ms[] = {
        0u, 1u, 3u, 5u, 10u, 25u, 50u, 80u, 100u
    };
    const uint8_t selected = lcd_settings_value_index[SETTING_LED_TIME];
    uint8_t duration = duration_ms[selected];

    if (selected == 0u) {
        const uint8_t solar = lcd_displayed_solar_percent;
        duration = solar >= 30u ? 100u :
            (uint8_t)(5u + ((uint16_t)solar * 95u) / 30u);
    }
    rgb_flash_active = true;
    return duration;
}

void board_rgb_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    rgb_level[0] = red;
    rgb_level[1] = green;
    rgb_level[2] = blue;
    rgb_channels = (red ? 1u : 0u) | (green ? 2u : 0u) | (blue ? 4u : 0u);
    board_rgb_apply();
}

void SysTick_Handler(void)
{
    const uint8_t elapsed_ms = systick_period_ms;

    /* The idle tick is 10 ms; preserve the monotonic millisecond timebase. */
    board_millis += elapsed_ms;
    board_uptime_millisecond_remainder += elapsed_ms;
    if (board_uptime_millisecond_remainder >= 1000u) {
        board_uptime_millisecond_remainder -= 1000u;
        ++board_uptime_seconds;
    }
}
