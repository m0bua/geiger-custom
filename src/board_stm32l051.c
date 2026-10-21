#include "board.h"
#include "board_config.h"
#include "font_large.h"
#include "font_small.h"

#include <stdbool.h>
#include <stdint.h>

volatile uint32_t board_millis;
static volatile uint32_t board_uptime_seconds;
static volatile uint16_t board_uptime_millisecond_remainder;
static volatile uint32_t detector_pulse_count;
static volatile bool board_hv_enabled = true;
static volatile uint16_t lcd_adc_battery_raw;
static volatile uint16_t lcd_adc_sun_raw;
static volatile uint16_t lcd_adc_usb_raw;
static bool adc_ready;
static volatile bool power_off_wakeup;
/* Runtime-only indication mode: INVISIBLE, NORMAL, SEARCH. */
static uint8_t lcd_indication_mode = 1u;

#define DETECTOR_GPIO_BASE GPIOB_BASE
#define DETECTOR_PIN       BOARD_DETECTOR_PIN
#define DETECTOR_EXTI_MASK (1u << DETECTOR_PIN)
#define FLASHLIGHT_PIN     8u
#define LCD_BATTERY_PERCENT_STABLE_MS 90000u

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define RCC_BASE       0x40021000u
#define RCC_CR         REG32(RCC_BASE + 0x00u)
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
#define LONG_PRESS_TICKS 80u
#define LCD_QUEUE_CAPACITY 192u
#define LCD_SPI_ENABLED 1u
#define LCD_DIAGNOSTIC_COMMANDS 1u
#define LCD_WIDTH 160u
#define LCD_HEIGHT 80u
#define LCD_PANEL_Y_OFFSET 24u
#define LCD_Y_OFFSET 28u
#define LCD_TEXT_COLUMNS 6u
#define LCD_TEXT_ROWS 2u
#define LCD_TEXT_GAP 3u
#define LCD_TEXT_ROW_GAP 6u
#define LCD_SMALL_TEXT_COLUMNS 6u
#define LCD_SMALL_TEXT_ROWS 6u
#define LCD_SMALL_TEXT_GAP 1u
#define LCD_SMALL_TEXT_ROW_GAP 2u
#define LCD_SMALL_TEXT_ROW_WIDTH (LCD_SMALL_TEXT_COLUMNS * \
    FONT_SMALL_CELL_WIDTH + (LCD_SMALL_TEXT_COLUMNS - 1u) * \
    LCD_SMALL_TEXT_GAP)
#define LCD_SMALL_TEXT_ROW_PIXELS (LCD_SMALL_TEXT_ROW_WIDTH * \
    FONT_SMALL_CELL_HEIGHT)
#define LCD_EXTRA_SCREEN_COUNT 3u
#define LCD_TEXT_FIRST_X ((LCD_WIDTH - (LCD_TEXT_COLUMNS * \
    FONT_LARGE_CELL_WIDTH + (LCD_TEXT_COLUMNS - 1u) * LCD_TEXT_GAP)) / 2u)
#define LCD_SMALL_TEXT_FIRST_Y ((LCD_HEIGHT - (LCD_SMALL_TEXT_ROWS * \
    FONT_SMALL_CELL_HEIGHT + (LCD_SMALL_TEXT_ROWS - 1u) * \
    LCD_SMALL_TEXT_ROW_GAP) + 1u) / 2u)
#define LCD_TEXT_FIRST_Y ((LCD_HEIGHT - (LCD_TEXT_ROWS * FONT_LARGE_CELL_HEIGHT + \
    (LCD_TEXT_ROWS - 1u) * LCD_TEXT_ROW_GAP)) / 2u)
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
#define LCD_WARMUP_TEXT_LENGTH 6u
#define LCD_WARMUP_BOX_WIDTH 64u
#define LCD_WARMUP_BOX_HEIGHT (FONT_SMALL_CELL_HEIGHT + 2u)
#define LCD_ADC_TEXT_LENGTH 18u
#define LCD_RATE_LABEL_WIDTH (LCD_RATE_LABEL_LENGTH * \
    FONT_SMALL_CELL_WIDTH + (LCD_RATE_LABEL_LENGTH - 1u) * \
    LCD_SMALL_TEXT_GAP)
#define LCD_CPM_TEXT_WIDTH (LCD_CPM_TEXT_LENGTH * FONT_SMALL_CELL_WIDTH + \
    (LCD_CPM_TEXT_LENGTH - 1u) * LCD_SMALL_TEXT_GAP)
#define LCD_COUNTER_GREEN 0x07E0u
#define LCD_COUNTER_YELLOW 0xFFE0u
#define LCD_COUNTER_RED 0xF800u
#define LCD_COUNTER_CYAN 0x07FFu
#define LCD_COUNTER_PURPLE 0x001Fu
#define LCD_COUNTER_LIGHT_GRAY 0xC618u
#define LCD_LARGE_GLYPH_PIXELS (FONT_LARGE_CELL_WIDTH * FONT_LARGE_CELL_HEIGHT)
#define LCD_TOP_ROW_PIXELS (LCD_WIDTH * FONT_SMALL_CELL_HEIGHT)
#define LCD_TEXT_GLYPH_PIXELS ((LCD_LARGE_GLYPH_PIXELS > \
    LCD_TOP_ROW_PIXELS) ? LCD_LARGE_GLYPH_PIXELS : \
    LCD_TOP_ROW_PIXELS)
#define LCD_TEXT_FOREGROUND 0xD6BAu
#define LCD_TEXT_BACKGROUND 0x0000u
#define LCD_TEXT_GREEN 0x07E0u
#define LCD_TEXT_YELLOW 0xFFE0u
#define LCD_TEXT_RED 0xF800u
#define LCD_SETTINGS_SELECTED_COLOR 0x07EEu
#define LCD_SETTINGS_ACTION_COLOR 0x7BE0u
#define LCD_SETTINGS_WARNING_COLOR 0x7800u

static void adc_wait_ms(uint32_t duration);
static void lcd_apply_brightness_settings(void);
static bool button_pressed(uint32_t base, uint32_t pin);
static void pin_input_with_pull(uint32_t base, uint32_t pin);

#define SYST_CSR       REG32(0xE000E010u)
#define SYST_RVR       REG32(0xE000E014u)
#define SYST_CVR       REG32(0xE000E018u)

typedef struct {
    uint8_t stable;
    uint8_t candidate;
    uint8_t count;
    uint8_t long_reported;
    uint16_t held_ticks;
    uint8_t repeat_ticks;
} button_debounce_t;

static button_debounce_t left_button;
static button_debounce_t right_button;
static uint8_t rgb_brightness_percent = 100u;
static uint8_t backlight_brightness_percent = 100u;
static bool backlight_pwm_running;
static bool backlight_requested_on;
static uint8_t rgb_channels;
static uint8_t rgb_level[3] = {255u, 255u, 255u};
static bool lcd_spi_ready = true;
static bool lcd_spi_error;
static bool lcd_control_error;
static bool lcd_started;
static bool lcd_ready;
static uint8_t lcd_init_state;
static uint32_t lcd_init_deadline;
static bool lcd_background_pending;
static bool lcd_fill_start_pending;
static bool lcd_fill_active;
static bool lcd_dma_active;
static bool lcd_dma_is_text_row;
static uint8_t lcd_dma_text_glyph_advance;
static bool lcd_text_active;
static bool lcd_cpm_redraw_pending;
static bool lcd_time_redraw_pending;
static bool lcd_debug_redraw_pending;
static bool lcd_settings_redraw_pending;
static bool lcd_text_row_commands_queued;
static uint8_t lcd_text_glyph_index;
static uint8_t lcd_text_dma_glyph_advance;
static uint16_t lcd_text_glyph_pixels;
static char lcd_rate_glyphs[LCD_RATE_MAX_GLYPHS + 2u] = ".0000";
static uint8_t lcd_rate_glyph_count = LCD_RATE_MAX_GLYPHS;
static uint16_t lcd_rate_color = LCD_COUNTER_PURPLE;
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
static uint16_t lcd_displayed_solar_percent;
static bool lcd_solar_sample_ready;
static uint8_t lcd_screen_page;
static uint8_t lcd_main_return_page;
static bool lcd_settings_list_active;
static uint8_t lcd_settings_selected;
static bool lcd_settings_editing;
static uint8_t lcd_settings_edit_value;
static bool rtc_ready;
static bool rtc_time_valid;
static uint8_t rtc_init_error;
static bool lcd_time_save_failed;
static bool lcd_time_save_succeeded;
static uint8_t lcd_time_save_error;
static uint32_t lcd_time_save_status_deadline;
static uint16_t lcd_time_edit_value;
static uint16_t lcd_time_edit_original;
static uint8_t lcd_settings_value_index[21] = {
    [0] = 2u, [7] = 2u, [10] = 1u, [11] = 1u
};
static bool click_sound_active;
static uint8_t click_sound_profile;
static uint8_t click_sound_stage;
static uint32_t click_sound_deadline;
static bool sound_alarm_mode;
static uint8_t sound_alarm_phase;

static uint32_t board_rgb_tim2_compare(void)
{
    const uint32_t period = click_sound_active ? 311u : PWM_PERIOD;
    uint32_t compare = ((uint32_t)rgb_brightness_percent * period) / 100u;
    if (click_sound_active && compare > 310u) compare = 310u;
    return compare;
}

static uint32_t board_rgb_channel_compare(uint8_t channel)
{
    return (board_rgb_tim2_compare() * rgb_level[channel]) / 255u;
}

static void board_rgb_apply(void)
{
    REG32(TIM2_BASE + TIM_CCR1) = (rgb_channels & 1u) ? board_rgb_channel_compare(0u) : 0u;
    REG32(TIM2_BASE + TIM_CCR2) = (rgb_channels & 2u) ? board_rgb_channel_compare(1u) : 0u;
    REG32(TIM22_BASE + TIM_CCR1) = (rgb_channels & 4u) ? board_rgb_channel_compare(2u) : 0u;
}
static bool flashlight_enabled;
static bool flashlight_manual_enabled;
static bool flashlight_alarm_override;
static bool flashlight_alarm_output;
static uint32_t lcd_time_displayed_minute = UINT32_MAX;
static uint32_t lcd_rate_value_thousandths;
static uint32_t lcd_current_cpm;
static uint32_t lcd_warmup_pulses;
static bool lcd_rate_window_ready;
static bool lcd_warmup_box_visible;
static uint32_t lcd_count_total;
static uint8_t lcd_settings_sequence;
static uint8_t lcd_timer_sequence;
static uint8_t lcd_alarm_sequence;
static uint8_t lcd_light_alarm_sequence;
static uint8_t lcd_dose_alarm_sequence;
static uint8_t lcd_hv_sequence;
static uint32_t lcd_dose_value_thousandths;
static uint32_t lcd_dose_fraction;
static uint32_t lcd_dose_measurement_seconds;
static uint32_t lcd_session_average_rate_thousandths;
static uint32_t lcd_session_maximum_rate_thousandths;
static uint32_t lcd_session_average_cpm;
static uint32_t lcd_session_maximum_cpm;
static uint32_t lcd_session_count_total;
static uint8_t lcd_dose_stats_mode;
static uint16_t lcd_text_glyph[LCD_TEXT_GLYPH_PIXELS];
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

static void lcd_check_output_level(uint32_t base, uint32_t pin, bool expected_high)
{
    bool actual_high = ((REG32(base + GPIO_IDR) >> pin) & 1u) != 0u;
    if (actual_high != expected_high) {
        lcd_control_error = true;
    }
}

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

static rtc_calendar_t lcd_time_draft;
static bool lcd_time_draft_ready;

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

static void rtc_get_time_edit_base(rtc_calendar_t *calendar)
{
    /* A first-time/invalid RTC edit starts from this firmware build timestamp. */
    if (!rtc_read_calendar(calendar)) {
        rtc_seed_from_build(calendar);
    }
}

static bool rtc_write_calendar(const rtc_calendar_t *calendar,
                               bool mark_time_valid)
{
    uint32_t timeout = 20000000u;
    lcd_time_save_error = 0u;
    if (!rtc_ready) {
        /* Retry a startup that may have raced RTC oscillator stabilization. */
        board_rtc_init();
        if (!rtc_ready) {
            lcd_time_save_error = rtc_init_error != 0u ?
                (uint8_t)(rtc_init_error + 4u) : 1u;
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
        lcd_time_save_error = 2u;
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
        lcd_time_save_error = 3u;
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

static void lcd_format_scaled_sv(uint64_t value_picosv_x18,
                                 uint8_t prefix_setting,
                                 bool dose_value)
{
    /* Scales are multiplied by 18 to retain the dose accumulator's
     * 1/18-pSv remainder without floating-point arithmetic. */
    static const uint64_t unit_scales[] = {
        18000000000ull, 18000000u, 18000u, 18u
    };
    static const char unit_prefixes[] = { 'm', (char)0xB5u, 'n', 'p' };
    uint8_t unit = 3u;
    uint8_t fallback_unit = 3u;
    const uint8_t selected_prefix = lcd_settings_value_index[prefix_setting];

    /* The stock firmware's R display is its conventional x100 scaling of Sv. */
    if (lcd_settings_value_index[9] == 2u) {
        value_picosv_x18 *= 100u;
    }
    bool has_fallback = false;

    if (selected_prefix < 3u) {
        unit = selected_prefix;
    } else {
        for (uint8_t i = 0u; i < 4u; ++i) {
            const uint64_t whole = value_picosv_x18 / unit_scales[i];
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
        const uint64_t whole = value_picosv_x18 / unit_scales[unit];
        if (whole < 10u || whole >= 1000u) {
            unit = fallback_unit;
        }
    }

    for (;;) {
        const uint64_t scale = unit_scales[unit];
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
                    (lcd_settings_value_index[9] == 2u ? 1u : 3u) :
                    (unit == 2u ? 0u : 5u)));
        if (dose_value && whole == 0u &&
            lcd_settings_value_index[8] == 0u && decimal_limit > 4u) {
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
            lcd_settings_value_index[8] == 0u) {
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
        if (lcd_settings_value_index[9] == 2u) {
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
    if (lcd_settings_value_index[10] == 0u) {
        lcd_format_count_value(cpm, "CPM  ");
    } else {
        const uint32_t thousandths_cps = (uint32_t)(
            ((uint64_t)cpm * 1000u + 30u) / 60u);
        const uint32_t whole = thousandths_cps / 1000u;
        const uint32_t fraction = thousandths_cps % 1000u;

        lcd_format_count_value(whole, "CPS  ");
        if (whole == 0u && lcd_settings_value_index[8] != 0u) {
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
    if (lcd_settings_value_index[9] == 1u) {
        lcd_format_count_rate(lcd_current_cpm);
        return;
    }
    if (rate_thousandths > LCD_RATE_MAX_VALUE) {
        rate_thousandths = LCD_RATE_MAX_VALUE;
    }
    /* Input is in 0.001 µSv/h (nSv/h), in units of 1/18 pSv. */
    lcd_format_scaled_sv((uint64_t)rate_thousandths * 18000u, 10u, false);
}

static void lcd_format_cpm(uint32_t cpm)
{
    const uint8_t row_mode = lcd_settings_value_index[12];

    for (uint8_t i = 0u; i < LCD_CPM_TEXT_LENGTH; ++i) {
        lcd_cpm_text[i] = ' ';
    }
    if (row_mode == 0u || row_mode == 1u) {
        if (row_mode == 1u) {
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
    } else if (row_mode >= 3u) {
        char saved_glyphs[sizeof(lcd_rate_glyphs)];
        char saved_label[LCD_RATE_LABEL_LENGTH];
        uint8_t saved_count = lcd_rate_glyph_count;
        uint8_t saved_unit = lcd_settings_value_index[9];
        uint8_t saved_prefix = lcd_settings_value_index[10];
        uint32_t saved_rate = lcd_rate_value_thousandths;
        char saved_unit_prefix = lcd_display_unit_prefix;
        const uint8_t row_unit = saved_unit == 1u ?
            (uint8_t)(row_mode == 3u ? 0u : 2u) :
            (uint8_t)(saved_unit == 0u ? 2u : 0u);
        uint8_t position = 0u;

        for (uint8_t i = 0u; i < sizeof(saved_glyphs); ++i) {
            saved_glyphs[i] = lcd_rate_glyphs[i];
        }
        for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) {
            saved_label[i] = lcd_rate_label[i];
        }
        lcd_settings_value_index[9] = row_unit;
        if (saved_unit == 1u) lcd_settings_value_index[10] = 3u;
        lcd_format_rate(saved_rate);
        while (position < LCD_CPM_TEXT_LENGTH &&
               position < lcd_rate_glyph_count) {
            lcd_cpm_text[position] = lcd_rate_glyphs[position];
            ++position;
        }
        for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH &&
             position < LCD_CPM_TEXT_LENGTH; ++i) {
            if (lcd_rate_label[i] != ' ') {
                lcd_cpm_text[position++] = lcd_rate_label[i];
            }
        }

        for (uint8_t i = 0u; i < sizeof(saved_glyphs); ++i) {
            lcd_rate_glyphs[i] = saved_glyphs[i];
        }
        for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) {
            lcd_rate_label[i] = saved_label[i];
        }
        lcd_rate_glyph_count = saved_count;
        lcd_settings_value_index[9] = saved_unit;
        lcd_settings_value_index[10] = saved_prefix;
        lcd_rate_value_thousandths = saved_rate;
        lcd_display_unit_prefix = saved_unit_prefix;
    }
    lcd_cpm_text[LCD_CPM_TEXT_LENGTH] = '\0';
}

static void lcd_format_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction)
{
    if (lcd_settings_value_index[9] == 1u) {
        lcd_format_count_value(lcd_count_total, "CNT  ");
        return;
    }
    /* The whole part is in nSv; the remainder is in 1/3600 nSv.
     * One accumulator step is 5/18 pSv; keep that fraction exactly. */
    const uint64_t value_picosv_x18 =
        (uint64_t)dose_thousandths * 18000u +
        (uint64_t)dose_fraction * 5u;
    lcd_format_scaled_sv(value_picosv_x18, 11u, true);
}

static uint8_t lcd_format_session_stats(char *text, uint8_t mode)
{
    char saved_glyphs[sizeof(lcd_rate_glyphs)];
    char saved_label[LCD_RATE_LABEL_LENGTH];
    uint8_t saved_glyph_count = lcd_rate_glyph_count;
    uint32_t saved_cpm = lcd_current_cpm;
    char saved_prefix = lcd_display_unit_prefix;
    uint8_t saved_unit = lcd_settings_value_index[9];
    const char *tag = mode == 1u ? "AVG " :
        (mode == 2u ? "MAX " : "CNT ");
    const uint32_t rate = mode == 1u ?
        lcd_session_average_rate_thousandths :
        lcd_session_maximum_rate_thousandths;
    const uint32_t cpm = mode == 1u ? lcd_session_average_cpm :
        lcd_session_maximum_cpm;
    uint8_t length = 0u;

    for (uint8_t i = 0u; i < sizeof(saved_glyphs); ++i) {
        saved_glyphs[i] = lcd_rate_glyphs[i];
    }
    for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) {
        saved_label[i] = lcd_rate_label[i];
    }
    for (uint8_t i = 0u; i < 4u; ++i) text[length++] = tag[i];

    if (mode == 3u) {
        lcd_format_count_value(lcd_session_count_total, "CNT  ");
    } else {
        lcd_current_cpm = cpm;
        /* Session AVG/MAX are radiation rates, even when the dose display
         * itself is configured to show counts. Do not label these as CPM. */
        if (saved_unit == 1u) lcd_settings_value_index[9] = 0u;
        lcd_format_rate(rate);
    }
    for (uint8_t i = 0u; i < lcd_rate_glyph_count; ++i) {
        text[length++] = lcd_rate_glyphs[i];
    }
    text[length] = '\0';

    for (uint8_t i = 0u; i < sizeof(saved_glyphs); ++i) {
        lcd_rate_glyphs[i] = saved_glyphs[i];
    }
    for (uint8_t i = 0u; i < LCD_RATE_LABEL_LENGTH; ++i) {
        lcd_rate_label[i] = saved_label[i];
    }
    lcd_rate_glyph_count = saved_glyph_count;
    lcd_current_cpm = saved_cpm;
    lcd_display_unit_prefix = saved_prefix;
    lcd_settings_value_index[9] = saved_unit;
    return length;
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
    if (lcd_queue_tail >= LCD_QUEUE_CAPACITY) {
        if (lcd_queue_head != lcd_queue_tail) {
            lcd_spi_ready = false;
            return false;
        }
        lcd_queue_head = 0u;
        lcd_queue_tail = 0u;
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

static void lcd_configure(void)
{
    lcd_spi_ready = true;

    /* Keep CS inactive until the stock PB7 board-enable settling interval
     * completes. LCD_Init then selects the panel before resetting it. */
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u;
    REG32(GPIOA_BASE + GPIO_MODER) = (REG32(GPIOA_BASE + GPIO_MODER) &
        ~(3u << 22u)) | (1u << 22u);
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u;
    lcd_check_output_level(GPIOA_BASE, 11u, true);

    /* PA8 reset output. */
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 8u;
    REG32(GPIOA_BASE + GPIO_MODER) = (REG32(GPIOA_BASE + GPIO_MODER) &
        ~(3u << 16u)) | (1u << 16u);
    lcd_check_output_level(GPIOA_BASE, 8u, true);
    /* Keep PB11 D/C high (data) while idle. */
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 11u;
    REG32(GPIOB_BASE + GPIO_MODER) = (REG32(GPIOB_BASE + GPIO_MODER) &
        ~(3u << 22u)) | (1u << 22u);
    lcd_check_output_level(GPIOB_BASE, 11u, true);
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
                              bool increment_source, bool text_row,
                              uint8_t text_glyph_advance)
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
                   (2u << 12u) | (increment_source ? (1u << 7u) : 0u);
    REG32(SPI2_BASE + SPI_CR1) &= ~(1u << 6u);
    REG32(SPI2_BASE + SPI_CR1) |= 1u << 11u; /* DFF: 16-bit frames */
    REG32(SPI2_BASE + SPI_CR1) |= 1u << 6u;
    REG32(SPI2_BASE + SPI_CR2) |= 1u << 1u; /* TXDMAEN */
    DMA1_CH5_CCR |= 1u;
    lcd_dma_active = true;
    lcd_dma_is_text_row = text_row;
    lcd_dma_text_glyph_advance = text_glyph_advance;
}

static void lcd_spi_pump(void)
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
            lcd_spi_error = true;
            lcd_spi_ready = false;
            return;
        }
        if ((status & (1u << 17u)) == 0u) {
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
                lcd_spi_error = true;
                lcd_spi_ready = false;
                return;
            }
        }
        (void)*(volatile uint16_t *)(uintptr_t)(SPI2_BASE + SPI_DR);
        (void)REG32(SPI2_BASE + SPI_SR);
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
        lcd_check_output_level(GPIOA_BASE, 11u, true);
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
        lcd_check_output_level(GPIOB_BASE, 11u, item.is_data != 0u);
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
        lcd_check_output_level(GPIOB_BASE, 11u, true);
        lcd_spi_dma_start(&lcd_draw_pixel,
            LCD_WIDTH * (LCD_Y_OFFSET + LCD_HEIGHT - LCD_PANEL_Y_OFFSET),
            false, false, 0u);
    }
}

static void lcd_send_controller_config(void)
{
    static const uint8_t frame[] = {0x01u, 0x2Cu, 0x2Du};
    static const uint8_t frame3[] = {0x01u, 0x2Cu, 0x2Du, 0x01u, 0x2Cu, 0x2Du};
    static const uint8_t power1[] = {0xA2u, 0x02u, 0x84u};
    static const uint8_t power2[] = {0x0Au, 0x00u};
    static const uint8_t power3[] = {0x8Au, 0x2Au};
    static const uint8_t power4[] = {0x8Au, 0xEEu};
    static const uint8_t madctl[] = {0xA8u};
    static const uint8_t colmod[] = {0x05u};
    static const uint8_t gamma_pos[] = {
        0x02u, 0x1Cu, 0x07u, 0x12u, 0x37u, 0x32u, 0x29u, 0x2Du,
        0x29u, 0x25u, 0x2Bu, 0x39u, 0x00u, 0x01u, 0x03u, 0x10u
    };
    static const uint8_t gamma_neg[] = {
        0x03u, 0x1Du, 0x07u, 0x06u, 0x2Eu, 0x2Cu, 0x29u, 0x2Du,
        0x2Eu, 0x2Eu, 0x37u, 0x3Fu, 0x00u, 0x00u, 0x02u, 0x10u
    };
    /* Match the stock controller init window (80 x 160, before rotation).
     * Drawing later sets the actual 160 x 80 window with its Y offset. */
    static const uint8_t x_window[] = {0x00u, 0x00u, 0x00u, 0x4Fu};
    static const uint8_t y_window[] = {0x00u, 0x00u, 0x00u, 0x9Fu};

    lcd_command(0xB1u, frame, sizeof(frame));
    lcd_command(0xB2u, frame, sizeof(frame));
    lcd_command(0xB3u, frame3, sizeof(frame3));
    { const uint8_t value = 0x07u; lcd_command(0xB4u, &value, 1u); }
    lcd_command(0xC0u, power1, sizeof(power1));
    { const uint8_t value = 0xC5u; lcd_command(0xC1u, &value, 1u); }
    lcd_command(0xC2u, power2, sizeof(power2));
    lcd_command(0xC3u, power3, sizeof(power3));
    lcd_command(0xC4u, power4, sizeof(power4));
    { const uint8_t value = 0x0Eu; lcd_command(0xC5u, &value, 1u); }
    lcd_command(0x20u, 0, 0u); /* INVOFF */
    lcd_command(0x36u, madctl, sizeof(madctl));
    lcd_command(0x3Au, colmod, sizeof(colmod));
    lcd_command(0x2Au, x_window, sizeof(x_window));
    lcd_command(0x2Bu, y_window, sizeof(y_window));
    lcd_command(0x21u, 0, 0u); /* INVON, matching the stock LCD sequence */
    lcd_command(0xE0u, gamma_pos, sizeof(gamma_pos));
    lcd_command(0xE1u, gamma_neg, sizeof(gamma_neg));
}

void board_lcd_backlight_set(bool enabled)
{
    if (enabled) {
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
    lcd_check_output_level(GPIOA_BASE, 11u, false);
    lcd_command(0x2Au, x_window, sizeof(x_window));
    lcd_command(0x2Bu, y_window, sizeof(y_window));
    lcd_command(0x2Cu, 0, 0u); /* RAMWR */
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
                lcd_text_glyph[(uint16_t)y * row_width + pixel_x] =
                    color;
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

static void lcd_debug_put_right(char *line, uint8_t end, const char *value,
                                uint8_t length)
{
    uint8_t start = length < end ? (uint8_t)(end - length) : 0u;
    for (uint8_t i = 0u; i < length && start + i < end; ++i) {
        line[start + i] = value[i];
    }
}

static uint8_t lcd_debug_make_line(uint8_t row, char *line)
{
    char value[20];
    uint8_t value_length = 0u;
    uint32_t seconds = board_uptime_seconds;

    for (uint8_t i = 0u; i < 20u; ++i) line[i] = ' ';

    if (row == 0u) {
        const uint32_t days = seconds / 86400u;
        const uint32_t minutes = (seconds / 60u) % 1440u;
        static const char prefix[] = "UPTIME ";
        for (uint8_t i = 0u; i < sizeof(prefix) - 1u; ++i) line[i] = prefix[i];
        value_length = lcd_debug_u32_digits(value, days);
        value[value_length++] = 'D'; value[value_length++] = ' ';
        value[value_length++] = (char)('0' + minutes / 600u);
        value[value_length++] = (char)('0' + (minutes / 60u) % 10u);
        value[value_length++] = ':';
        value[value_length++] = (char)('0' + (minutes % 60u) / 10u);
        value[value_length++] = (char)('0' + minutes % 10u);
        lcd_debug_put_right(line, 20u, value, value_length);
    } else if (row == 1u) {
        const char *const status = rtc_init_error == 1u ? "OSC" :
            rtc_init_error == 2u ? "INT" : rtc_init_error == 3u ? "SYC" :
            rtc_init_error == 4u ? "DBP" : !rtc_ready ? "---" :
            ((RCC_CSR >> 16u) & 3u) == 1u ? "LSE" : "LSI";
        for (uint8_t i = 0u; i < 3u; ++i) line[i] = status[i];
        line[4] = 'L';
        line[5] = (RCC_CSR & 1u) != 0u ? '1' : '0';
        line[6] = 'R';
        line[7] = (RCC_CSR & (1u << 1u)) != 0u ? '1' : '0';
        line[9] = 'A';
        line[10] = (REG32(GPIOA_BASE + 0x14u) & (1u << 5u)) != 0u ?
            '1' : '0';
        line[11] = 'B';
        line[12] = (REG32(GPIOB_BASE + GPIO_IDR) & DETECTOR_EXTI_MASK) != 0u ?
            '1' : '0';
        line[14] = 'M';
        line[15] = (EXTI_IMR & DETECTOR_EXTI_MASK) != 0u ? '1' : '0';
        line[16] = 'F';
        line[17] = (EXTI_FTSR & DETECTOR_EXTI_MASK) != 0u ? '1' : '0';
    } else if (row == 2u) {
        static const char label[] = "CNT ";
        for (uint8_t i = 0u; i < sizeof(label) - 1u; ++i) line[i] = label[i];
        value_length = lcd_debug_u32_digits(value, detector_pulse_count);
        lcd_debug_put_right(line, 20u, value, value_length);
    } else {
        const char *label = row == 3u ? "BATTERY" :
            (row == 4u ? "SUN" : "USB");
        const uint8_t label_length = row == 3u ? 7u : 3u;
        const uint16_t raw = row == 3u ? lcd_adc_battery_raw :
            (row == 4u ? lcd_adc_sun_raw : lcd_adc_usb_raw);
        for (uint8_t i = 0u; i < label_length; ++i) line[i] = label[i];
        if (row != 5u) {
            const uint16_t percent = row == 3u ? lcd_displayed_battery_percent :
                lcd_displayed_solar_percent;
            value_length = lcd_debug_u32_digits(value, percent);
            value[value_length++] = '%';
            lcd_debug_put_right(line, 14u, value, value_length);
        }
        value_length = lcd_debug_u32_digits(value, raw);
        lcd_debug_put_right(line, 20u, value, value_length);
    }
    return 20u;
}

#define LCD_SETTINGS_COUNT 21u
#define LCD_SETTINGS_VISIBLE_ROWS 6u
#define LCD_SETTINGS_POWER_OFF_ITEM 18u
#define LCD_SETTINGS_REBOOT_ITEM 19u

static const char *const lcd_settings_labels[LCD_SETTINGS_COUNT] = {
    "LED", "CLICKS", "SCREEN ALARM", "LIGHT ALARM", "SOUND ALARM", "LED BRIGHT",
    "SCREEN BRIGHT", "SCREEN TIMER", "LEAD ZERO", "UNIT", "RATE PREFIX",
    "DOSE PREF", "RATE ROW 2", "USE-SPI", "RESET", "HV", "DOSE ALARM",
    "ALARM STOP", "POWER OFF", "REBOOT", "TONE"
};

static const char *const lcd_settings_values[LCD_SETTINGS_COUNT][11] = {
    { "OFF", "ON", "ALWAYS", "YELLOW", "RED" },
    { "OFF", "ON", "ALWAYS", "YELLOW", "RED" },
    { "OFF", "YELLOW", "RED" },
    { "OFF", "YELLOW", "RED" },
    { "OFF", "YELLOW", "RED" },
    { "AUTO", "1%", "3%", "5%", "10%", "25%", "50%", "80%", "100%" },
    { "AUTO", "1%", "3%", "5%", "10%", "25%", "50%", "80%", "100%" },
    { "1", "5", "10", "30", "60", "90" },
    { "SHOW", "HIDE" },
    { "SV", "CNT", "R" },
    { "m", "\xB5", "n", "AUTO", "CPM", "CPS" },
    { "m", "\xB5", "n", "AUTO", "CPM", "CPS" },
    { "CPM", "CPS", "OFF", "SV", "R" },
    { "OFF", "ON" },
    { "" },
    { "ON", "OFF" },
    { "OFF", "AUTO-Y", "AUTO-R", "5", "7", "10", "30", "50",
      "100", "500", "1000" },
    { "CLEAR", "BUTTON" },
    { "" },
    { "" },
    { "STOCK", "CLASSIC", "MODERN" }
};

static const uint8_t lcd_settings_value_counts[LCD_SETTINGS_COUNT] = {
    5u, 5u, 3u, 3u, 3u, 9u, 9u, 6u, 2u, 3u, 6u, 6u, 3u, 2u, 1u, 2u,
    11u, 2u, 1u, 1u, 3u
};

static uint8_t lcd_settings_value_count(uint8_t item)
{
    if (item == 5u || item == 6u) return 9u;
    if (item == 16u) return 11u;
    if (item == 10u || item == 11u) {
        return lcd_settings_value_index[9] == 1u ? 2u : 4u;
    }
    if (item == 12u) {
        return lcd_settings_value_index[9] == 1u ? 5u : 4u;
    }
    return lcd_settings_value_counts[item];
}

static const char *lcd_settings_value_text(uint8_t item)
{
    uint8_t value_index = lcd_settings_value_index[item];
    if (lcd_settings_editing && item == lcd_settings_selected) {
        value_index = lcd_settings_edit_value;
    }
    if ((item == 10u || item == 11u) &&
        lcd_settings_value_index[9] == 1u) {
        return lcd_settings_values[item][value_index + 4u];
    }
    if (item == 12u && value_index == 3u &&
        lcd_settings_value_index[9] != 1u) {
        value_index = lcd_settings_value_index[9] == 0u ? 4u : 3u;
    }
    return lcd_settings_values[item][value_index];
}

static uint8_t lcd_settings_visible_count(void)
{
    return (uint8_t)(LCD_SETTINGS_COUNT -
        (lcd_settings_value_index[9] == 1u ? 1u : 0u));
}

static uint8_t lcd_settings_visible_to_item(uint8_t visible_index)
{
    static const uint8_t item_order[LCD_SETTINGS_COUNT] = {
        0u, 1u, 2u, 3u, 4u, 16u, 17u, 5u, 6u, 7u, 20u,
        8u, 9u, 10u, 11u, 12u, 15u, 13u, 14u, 18u, 19u
    };
    uint8_t visible = 0u;

    for (uint8_t i = 0u; i < LCD_SETTINGS_COUNT; ++i) {
        const uint8_t item = item_order[i];
        if (lcd_settings_value_index[9] == 1u && item == 11u) continue;
        if (visible == visible_index) return item;
        ++visible;
    }
    return LCD_SETTINGS_COUNT;
}

static uint8_t lcd_settings_item_to_visible(uint8_t item)
{
    const uint8_t visible_count = lcd_settings_visible_count();

    for (uint8_t i = 0u; i < visible_count; ++i) {
        if (lcd_settings_visible_to_item(i) == item) return i;
    }
    return 0u;
}

static uint8_t lcd_settings_first_visible_item(void)
{
    const uint8_t selected_visible =
        lcd_settings_item_to_visible(lcd_settings_selected);
    uint8_t first = selected_visible > 2u ?
        (uint8_t)(selected_visible - 2u) : 0u;
    const uint8_t visible_count = lcd_settings_visible_count();
    const uint8_t last_first = visible_count > LCD_SETTINGS_VISIBLE_ROWS ?
        (uint8_t)(visible_count - LCD_SETTINGS_VISIBLE_ROWS) : 0u;

    return first > last_first ? last_first : first;
}

static uint8_t lcd_settings_make_line(uint8_t visible_row, char *line)
{
    const uint8_t first = lcd_settings_first_visible_item();
    const uint8_t item = lcd_settings_visible_to_item(
        (uint8_t)(first + visible_row));
    const char *label;
    const char *value;
    uint8_t label_length = 0u;
    uint8_t value_length = 0u;
    uint8_t value_start;

    for (uint8_t i = 0u; i < 20u; ++i) line[i] = ' ';
    if (item >= LCD_SETTINGS_COUNT) return 20u;
    line[0] = lcd_settings_list_active &&
        item == lcd_settings_selected && !lcd_settings_editing ? '>' : ' ';
    label = lcd_settings_labels[item];
    value = lcd_settings_value_text(item);
    while (label[label_length] != '\0' && label_length < 13u) ++label_length;
    while (value[value_length] != '\0' && value_length < 9u) ++value_length;
    for (uint8_t i = 0u; i < label_length && i + 1u < 20u; ++i) {
        line[i + 1u] = label[i];
    }
    value_start = value_length < 20u ? (uint8_t)(20u - value_length) : 0u;
    if (value_start <= label_length + 1u) {
        value_start = (uint8_t)(label_length + 2u);
    }
    if (lcd_settings_list_active && item == lcd_settings_selected &&
        lcd_settings_editing && value_start > 0u) {
        line[value_start - 1u] = '>';
    }
    for (uint8_t i = 0u; i < value_length && value_start + i < 20u; ++i) {
        line[value_start + i] = value[i];
    }
    return 20u;
}

static bool lcd_text_prepare_glyph(void)
{
    if (lcd_screen_page == 2u) {
        const uint8_t step = lcd_text_glyph_index;
        uint8_t x_window[4];
        uint8_t y_window[4];
        rtc_calendar_t calendar;
        const bool show_calendar = rtc_time_valid &&
            rtc_read_calendar(&calendar);

        if (lcd_settings_list_active) {
            static const char *const time_menu_items[] = {
                "YEAR", "MONTH", "DAY", "HOUR", "MINUTE", "SAVE"
            };
            char line[20];
            uint16_t field_value;
            uint8_t value_start;
            uint8_t value_length;

            if (step >= 6u) {
                lcd_text_active = false;
                return false;
            }
            if (!lcd_time_draft_ready) {
                rtc_get_time_edit_base(&lcd_time_draft);
                lcd_time_draft_ready = true;
            }
            for (uint8_t i = 0u; i < sizeof(line); ++i) line[i] = ' ';
            line[0] = step != 5u && step == lcd_settings_selected &&
                !lcd_settings_editing ? '>' : ' ';
            for (uint8_t i = 0u; time_menu_items[step][i] != '\0' &&
                 i < 13u; ++i) {
                line[i + 1u] = time_menu_items[step][i];
            }
            switch (step) {
            case 0u: field_value = lcd_time_draft.year; value_length = 4u; break;
            case 1u: field_value = lcd_time_draft.month; value_length = 2u; break;
            case 2u: field_value = lcd_time_draft.day; value_length = 2u; break;
            case 3u: field_value = lcd_time_draft.hour; value_length = 2u; break;
            case 4u: field_value = lcd_time_draft.minute; value_length = 2u; break;
            default: field_value = 0u; value_length = 0u; break;
            }
            if (lcd_settings_editing && step == lcd_settings_selected) {
                field_value = lcd_time_edit_value;
            }
            value_start = (uint8_t)(20u - value_length);
            if (step != 5u) {
                line[value_start - 1u] = lcd_settings_editing &&
                    step == lcd_settings_selected ? '>' : ' ';
            }
            for (uint8_t i = 0u; i < value_length; ++i) {
                const uint16_t divisor = value_length == 4u ?
                    (uint16_t)(i == 0u ? 1000u : i == 1u ? 100u :
                        i == 2u ? 10u : 1u) :
                    (uint16_t)(i == 0u ? 10u : 1u);
                line[value_start + i] =
                    (char)('0' + (field_value / divisor) % 10u);
            }
            if (step == 5u) {
                if (lcd_time_save_failed) {
                    const char *const error = lcd_time_save_error == 1u ?
                        "RDY " : lcd_time_save_error == 2u ? "INIT" :
                        lcd_time_save_error == 3u ? "SYNC" :
                        lcd_time_save_error == 4u ? "DRFT" :
                        lcd_time_save_error == 5u ? "OSC " :
                        lcd_time_save_error == 6u ? "INIT" :
                        lcd_time_save_error == 7u ? "SYNC" :
                        lcd_time_save_error == 8u ? "DBP " : "DATE";
                    for (uint8_t i = 0u; i < 4u; ++i) {
                        line[16u + i] = error[i];
                    }
                } else if (lcd_time_save_succeeded) {
                    line[18] = 'O';
                    line[19] = 'K';
                }
            }
            lcd_text_glyph_pixels = (uint16_t)(159u *
                FONT_SMALL_CELL_HEIGHT);
            lcd_text_dma_glyph_advance = 1u;
            for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
                for (uint16_t x = 0u; x < 159u; ++x) {
                    const uint8_t column = (uint8_t)(x / 8u);
                    const uint8_t glyph_x = (uint8_t)(x % 8u);
                    const bool lit = column < 20u &&
                        glyph_x < FONT_SMALL_CELL_WIDTH &&
                        font_small_pixel(line[column], glyph_x, y);
                    uint16_t color = LCD_TEXT_FOREGROUND;
                    if (step == 5u && lcd_time_save_failed &&
                        column >= 16u) {
                        color = LCD_TEXT_RED;
                    } else if (step == 5u && lcd_time_save_succeeded &&
                               column >= 18u) {
                        color = LCD_TEXT_GREEN;
                    } else if (lcd_settings_editing &&
                        step == lcd_settings_selected &&
                        column >= value_start - 1u) {
                        color = LCD_SETTINGS_SELECTED_COLOR;
                    } else if (!lcd_settings_editing &&
                               step == lcd_settings_selected &&
                               column <= 6u) {
                        color = LCD_SETTINGS_SELECTED_COLOR;
                    }
                    lcd_text_glyph[(uint16_t)y * 159u + x] = lit ? color :
                        LCD_TEXT_BACKGROUND;
                }
            }
            x_window[0] = 0u;
            x_window[1] = 0u;
            x_window[2] = 0u;
            x_window[3] = 158u;
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + 3u + step * 12u);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(y_window[1] +
                FONT_SMALL_CELL_HEIGHT - 1u);
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
            return true;
        }

        if (step >= 7u) {
            lcd_text_active = false;
            return false;
        }

        if (step == 0u || step == 6u) {
            char days_text[16];
            uint8_t days_length = 0u;
            uint32_t days = board_uptime_seconds / 86400u;

            if (step == 0u) {
                const char *header = show_calendar ? "TIME" : "UPTIME";
                days_length = show_calendar ? 4u : 6u;
                for (uint8_t i = 0u; i < days_length; ++i) {
                    days_text[i] = header[i];
                }
            } else if (show_calendar) {
                days_text[days_length++] = (char)('0' + calendar.year / 1000u);
                days_text[days_length++] = (char)('0' +
                    (calendar.year / 100u) % 10u);
                days_text[days_length++] = (char)('0' +
                    (calendar.year / 10u) % 10u);
                days_text[days_length++] = (char)('0' + calendar.year % 10u);
                days_text[days_length++] = '-';
                days_text[days_length++] = (char)('0' + calendar.month / 10u);
                days_text[days_length++] = (char)('0' + calendar.month % 10u);
                days_text[days_length++] = '-';
                days_text[days_length++] = (char)('0' + calendar.day / 10u);
                days_text[days_length++] = (char)('0' + calendar.day % 10u);
            } else {
                char reversed[10];
                uint8_t digit_count = 0u;
                do {
                    reversed[digit_count++] = (char)('0' + days % 10u);
                    days /= 10u;
                } while (days != 0u && digit_count < sizeof(reversed));
                while (digit_count != 0u) {
                    days_text[days_length++] = reversed[--digit_count];
                }
                days_text[days_length++] = ' ';
                days_text[days_length++] = 'D';
                days_text[days_length++] = 'A';
                days_text[days_length++] = 'Y';
                days_text[days_length++] = 'S';
            }

            lcd_text_glyph_pixels = LCD_TOP_ROW_PIXELS;
            lcd_text_dma_glyph_advance = 1u;
            for (uint16_t i = 0u; i < LCD_TOP_ROW_PIXELS; ++i) {
                lcd_text_glyph[i] = LCD_TEXT_BACKGROUND;
            }
            {
                const uint16_t text_width = (uint16_t)(days_length *
                    (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP) -
                    LCD_SMALL_TEXT_GAP);
                const uint16_t text_x = (uint16_t)((LCD_WIDTH -
                    text_width) / 2u);
                const uint8_t y_start = step == 0u ? 3u : 64u;

                for (uint8_t column = 0u; column < days_length; ++column) {
                    for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
                        for (uint8_t x = 0u; x < FONT_SMALL_CELL_WIDTH; ++x) {
                            const uint16_t pixel_x = text_x + column *
                                (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP) + x;
                            if (font_small_pixel(days_text[column], x, y)) {
                                lcd_text_glyph[(uint16_t)y * LCD_WIDTH + pixel_x] =
                                    LCD_TEXT_FOREGROUND;
                            }
                        }
                    }
                }
                x_window[0] = 0u;
                x_window[1] = 0u;
                x_window[2] = 0u;
                x_window[3] = (uint8_t)(LCD_WIDTH - 1u);
                y_window[0] = 0u;
                y_window[1] = (uint8_t)(LCD_Y_OFFSET + y_start);
                y_window[2] = 0u;
                y_window[3] = (uint8_t)(LCD_Y_OFFSET + y_start +
                    FONT_SMALL_CELL_HEIGHT - 1u);
            }
        } else {
            const uint32_t total_minutes = board_uptime_seconds / 60u;
            const uint32_t hour = show_calendar ? calendar.hour :
                (total_minutes / 60u) % 24u;
            const uint32_t minute = show_calendar ? calendar.minute :
                total_minutes % 60u;
            const char glyph = step == 3u ? ':' :
                (step < 3u ? (char)('0' + (step == 1u ? hour / 10u : hour % 10u)) :
                 (char)('0' + (step == 4u ? minute / 10u : minute % 10u)));
            const uint8_t glyph_x = (uint8_t)(20u + (step - 1u) *
                FONT_LARGE_CELL_WIDTH);

            lcd_text_glyph_pixels = FONT_LARGE_CELL_WIDTH *
                FONT_LARGE_CELL_HEIGHT;
            lcd_text_dma_glyph_advance = 1u;
            for (uint8_t y = 0u; y < FONT_LARGE_CELL_HEIGHT; ++y) {
                for (uint8_t x = 0u; x < FONT_LARGE_CELL_WIDTH; ++x) {
                    lcd_text_glyph[(uint16_t)y * FONT_LARGE_CELL_WIDTH + x] =
                        font_large_pixel(glyph, x, y) ?
                            LCD_TEXT_FOREGROUND : LCD_TEXT_BACKGROUND;
                }
            }
            x_window[0] = 0u;
            x_window[1] = glyph_x;
            x_window[2] = 0u;
            x_window[3] = (uint8_t)(glyph_x + FONT_LARGE_CELL_WIDTH - 1u);
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + 18u);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(LCD_Y_OFFSET + 18u +
                FONT_LARGE_CELL_HEIGHT - 1u);
        }

        REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
        lcd_command(0x2Au, x_window, sizeof(x_window));
        lcd_command(0x2Bu, y_window, sizeof(y_window));
        lcd_command(0x2Cu, 0, 0u);
        lcd_text_row_commands_queued = true;
        return true;
    }

    if (lcd_screen_page >= 3u) {
        if (lcd_screen_page == 3u) {
            char line[20];
            uint8_t x_window[4] = { 0u, 0u, 0u, 158u };
            uint8_t y_window[4];

            if (lcd_text_glyph_index >= LCD_SETTINGS_VISIBLE_ROWS) {
                lcd_text_active = false;
                return false;
            }
            (void)lcd_settings_make_line(lcd_text_glyph_index, line);
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + 3u +
                lcd_text_glyph_index * 12u);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(y_window[1] +
                FONT_SMALL_CELL_HEIGHT - 1u);
            lcd_text_glyph_pixels = (uint16_t)(159u *
                FONT_SMALL_CELL_HEIGHT);
            lcd_text_dma_glyph_advance = 1u;
            {
                const uint8_t item = lcd_settings_visible_to_item(
                    (uint8_t)(lcd_settings_first_visible_item() +
                        lcd_text_glyph_index));
                const bool selected_row = lcd_settings_list_active &&
                    item == lcd_settings_selected;
                const bool action_item = item == LCD_SETTINGS_POWER_OFF_ITEM ||
                    item == LCD_SETTINGS_REBOOT_ITEM;
                const bool warning_item = item == 13u || item == 14u;
                const bool value_edit_row = selected_row &&
                    lcd_settings_value_count(item) > 3u;
                uint8_t label_length = 0u;
                uint8_t value_length = 0u;
                uint8_t value_start;
                while (lcd_settings_labels[item][label_length] != '\0' &&
                       label_length < 13u) ++label_length;
                while (lcd_settings_value_text(item)[value_length] != '\0' &&
                       value_length < 9u) ++value_length;
                value_start = value_length < 20u ?
                    (uint8_t)(20u - value_length) : 0u;
                if (value_start <= label_length + 1u) {
                    value_start = (uint8_t)(label_length + 2u);
                }
                const uint16_t text_color = warning_item ?
                    (selected_row ? LCD_TEXT_RED :
                     LCD_SETTINGS_WARNING_COLOR) :
                    (action_item ?
                        (selected_row ? LCD_TEXT_YELLOW :
                         LCD_SETTINGS_ACTION_COLOR) :
                        (selected_row ? LCD_SETTINGS_SELECTED_COLOR :
                         LCD_TEXT_FOREGROUND));

            for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
                for (uint16_t x = 0u; x < 159u; ++x) {
                    const uint8_t column = (uint8_t)(x / 8u);
                    const uint8_t glyph_x = (uint8_t)(x % 8u);
                    const bool lit = column < 20u &&
                        glyph_x < FONT_SMALL_CELL_WIDTH &&
                        font_small_pixel(line[column], glyph_x, y);
                    uint16_t pixel_color = text_color;
                    if (value_edit_row) {
                        const bool label_char = column > 0u &&
                            column <= label_length;
                        const bool value_char = column >= value_start &&
                            column < value_start + value_length;
                        const bool value_cursor = lcd_settings_editing &&
                            value_start > 0u && column == value_start - 1u;
                        const bool highlighted = lcd_settings_editing ?
                            (value_char || value_cursor) :
                            (label_char || column == 0u);
                        pixel_color = highlighted ? LCD_SETTINGS_SELECTED_COLOR :
                            LCD_TEXT_FOREGROUND;
                    }
                    lcd_text_glyph[(uint16_t)y * 159u + x] = lit ?
                        pixel_color : LCD_TEXT_BACKGROUND;
                }
            }
            }
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
            return true;
        }
        if (lcd_screen_page == 4u) {
            char text[24];
            const uint8_t length = lcd_debug_make_line(
                lcd_text_glyph_index, text);
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

            if (lcd_text_glyph_index >= 6u) {
                lcd_text_active = false;
                return false;
            }
            lcd_text_glyph_pixels = (uint16_t)(row_width *
                FONT_SMALL_CELL_HEIGHT);
            lcd_text_dma_glyph_advance = 1u;
            for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
                for (uint16_t x = 0u; x < row_width; ++x) {
                    const uint8_t column = (uint8_t)(x /
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                    const uint8_t glyph_x = (uint8_t)(x %
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                    lcd_text_glyph[(uint16_t)y * row_width + x] =
                        column < length && glyph_x < FONT_SMALL_CELL_WIDTH &&
                        font_small_pixel(text[column], glyph_x, y) ?
                            LCD_TEXT_FOREGROUND : LCD_TEXT_BACKGROUND;
                }
            }
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
            return true;
        }
        static const char *const labels[2] = {
            "SETTINGS", "DEBUG"
        };
        const uint8_t label_index = (uint8_t)(lcd_screen_page - 3u);
        const char *text = labels[label_index];
        const uint8_t length = label_index == 0u ? 8u : 5u;
        const uint16_t row_width = (uint16_t)(length *
            FONT_SMALL_CELL_WIDTH + (length - 1u) * LCD_SMALL_TEXT_GAP);
        const uint8_t x_start = (uint8_t)((LCD_WIDTH - row_width) / 2u);
        const uint8_t y_start = (uint8_t)((LCD_HEIGHT -
            FONT_SMALL_CELL_HEIGHT) / 2u);
        uint8_t x_window[4] = {
            0u, x_start, 0u, (uint8_t)(x_start + row_width - 1u)
        };
        uint8_t y_window[4] = {
            0u, (uint8_t)(LCD_Y_OFFSET + y_start), 0u,
            (uint8_t)(LCD_Y_OFFSET + y_start + FONT_SMALL_CELL_HEIGHT - 1u)
        };

        if (lcd_text_glyph_index != 0u) {
            lcd_text_active = false;
            return false;
        }

        lcd_text_glyph_pixels = (uint16_t)(row_width *
            FONT_SMALL_CELL_HEIGHT);
        lcd_text_dma_glyph_advance = 1u;
        for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
            for (uint16_t x = 0u; x < row_width; ++x) {
                const uint8_t column = (uint8_t)(x /
                    (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                const uint8_t glyph_x = (uint8_t)(x %
                    (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                const uint16_t pixel_index = (uint16_t)y * row_width + x;

                lcd_text_glyph[pixel_index] =
                    column < length &&
                    glyph_x < FONT_SMALL_CELL_WIDTH &&
                    font_small_pixel(text[column], glyph_x, y) ?
                        LCD_TEXT_FOREGROUND : LCD_TEXT_BACKGROUND;
            }
        }

        REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
        lcd_command(0x2Au, x_window, sizeof(x_window));
        lcd_command(0x2Bu, y_window, sizeof(y_window));
        lcd_command(0x2Cu, 0, 0u);
        lcd_text_row_commands_queued = true;
        return true;
    }

    {
        const uint8_t rate_label_glyph = (uint8_t)(LCD_RATE_MAX_GLYPHS + 4u);
        const uint8_t cpm_glyph = (uint8_t)(LCD_RATE_MAX_GLYPHS + 5u);
        const bool warmup_active = lcd_screen_page == 0u &&
            lcd_settings_value_index[15] == 0u && !lcd_rate_window_ready;
        const bool show_cpm_row = lcd_screen_page == 0u &&
            lcd_settings_value_index[12] != 2u &&
            lcd_settings_value_index[15] == 0u;
        const bool show_dose_session_stats = lcd_screen_page == 1u &&
            lcd_dose_stats_mode != 0u;
        const bool clear_warmup_box = lcd_screen_page == 0u &&
            lcd_warmup_box_visible && !warmup_active && !show_cpm_row;
        const uint8_t last_glyph = (warmup_active || show_cpm_row ||
            clear_warmup_box || show_dose_session_stats) ? cpm_glyph :
                rate_label_glyph;
        uint8_t x_start;
        uint8_t y_start;
        uint8_t x_window[4];
        uint8_t y_window[4];
        char glyph;

        if (lcd_text_glyph_index > last_glyph) {
            lcd_text_active = false;
            return false;
        }

        if (lcd_text_glyph_index == 0u) {
            lcd_text_glyph_pixels = LCD_TOP_ROW_PIXELS;
            lcd_text_dma_glyph_advance = 1u;
            for (uint16_t i = 0u; i < LCD_TOP_ROW_PIXELS; ++i) {
                lcd_text_glyph[i] = LCD_TEXT_BACKGROUND;
            }
            lcd_top_row_draw_uptime();
            lcd_top_row_draw_mode_letter();
            lcd_top_row_draw_flashlight_letter();
            const uint16_t sun_color = lcd_displayed_solar_percent > 79u ?
                LCD_TEXT_GREEN : (lcd_displayed_solar_percent > 49u ?
                LCD_TEXT_YELLOW : LCD_TEXT_FOREGROUND);
            const uint16_t battery_color = lcd_adc_usb_raw > 1000u ?
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
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
            return true;
        }

        if (lcd_text_glyph_index == 1u || lcd_text_glyph_index == 2u) {
            /* These slots used to draw raw ADC values and clear the full
             * large-number field. Neither operation is needed anymore. */
            ++lcd_text_glyph_index;
            return false;
        }

        if (lcd_screen_page == 0u && lcd_settings_value_index[15] == 1u &&
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
            lcd_text_glyph_pixels = (uint16_t)(text_width *
                FONT_SMALL_CELL_HEIGHT);
            lcd_text_dma_glyph_advance =
                (uint8_t)(rate_label_glyph - lcd_text_glyph_index);
            for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
                for (uint16_t x = 0u; x < text_width; ++x) {
                    const uint8_t column = (uint8_t)(x /
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                    const uint8_t glyph_x = (uint8_t)(x %
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                    const uint16_t pixel =
                        (uint16_t)y * text_width + x;
                    lcd_text_glyph[pixel] = column < text_length &&
                        glyph_x < FONT_SMALL_CELL_WIDTH &&
                        font_small_pixel(disabled_text[column], glyph_x, y) ?
                            LCD_COUNTER_RED : LCD_TEXT_BACKGROUND;
                }
            }
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
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
                if (lcd_settings_value_index[9] == 1u) {
                    dose_label[0] = 'C';
                    dose_label[1] = 'N';
                    dose_label[2] = 'T';
                } else if (lcd_settings_value_index[9] == 2u) {
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

            lcd_text_glyph_pixels = (uint16_t)(label_width *
                FONT_SMALL_CELL_HEIGHT);
            lcd_text_dma_glyph_advance = 1u;
            for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
                for (uint8_t x = 0u; x < label_width; ++x) {
                    uint8_t column = (uint8_t)(x /
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                    uint8_t glyph_x = (uint8_t)(x %
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                    uint16_t pixel_index = (uint16_t)y * label_width + x;

                    lcd_text_glyph[pixel_index] =
                        column < label_length &&
                        glyph_x < FONT_SMALL_CELL_WIDTH &&
                        font_small_pixel(label[column], glyph_x, y) ?
                            (lcd_screen_page == 0u ? lcd_rate_color :
                                LCD_TEXT_FOREGROUND) : LCD_TEXT_BACKGROUND;
                }
            }

            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
            return true;
        }

        if (lcd_text_glyph_index == cpm_glyph &&
            (warmup_active || show_cpm_row || clear_warmup_box)) {
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
                lcd_text_glyph[i] = LCD_TEXT_BACKGROUND;
            }
            {
                char warmup_text[LCD_WARMUP_TEXT_LENGTH + 1u] = {
                    '0', '0', ' ', 'C', 'N', 'T', '\0'
                };
                const uint32_t count = lcd_warmup_pulses > 25u ? 25u :
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
                        (LCD_WARMUP_BOX_WIDTH - 2u) / 25u);
                    warmup_text[0] = (char)('0' + count / 10u);
                    warmup_text[1] = (char)('0' + count % 10u);
                    for (uint8_t y = 0u; y < LCD_WARMUP_BOX_HEIGHT; ++y) {
                        for (uint16_t x = 0u; x < LCD_WARMUP_BOX_WIDTH; ++x) {
                            const bool frame = y == 0u ||
                                y == LCD_WARMUP_BOX_HEIGHT - 1u ||
                                x == 0u || x == LCD_WARMUP_BOX_WIDTH - 1u;
                            const bool filled = !frame && x > 0u &&
                                x <= fill_width;
                            lcd_text_glyph[(uint16_t)y * text_width +
                                box_x + x] = frame ? LCD_COUNTER_LIGHT_GRAY :
                                (filled ? LCD_COUNTER_PURPLE :
                                    LCD_TEXT_BACKGROUND);
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
                                    lcd_text_glyph[pixel] =
                                        LCD_COUNTER_LIGHT_GRAY;
                                }
                            }
                        }
                    }
                    lcd_warmup_box_visible = true;
                } else if (show_cpm_row) {
                    for (uint8_t column = 0u;
                         column < LCD_CPM_TEXT_LENGTH; ++column) {
                        lcd_top_row_draw_glyph(lcd_cpm_text[column],
                            (uint16_t)(column * (FONT_SMALL_CELL_WIDTH +
                                LCD_SMALL_TEXT_GAP)), text_width,
                            LCD_COUNTER_LIGHT_GRAY);
                    }
                    lcd_warmup_box_visible = false;
                } else {
                    /* RATE ROW 2 is OFF: blank the old progress box. */
                    lcd_warmup_box_visible = false;
                }
            }

            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
            return true;
        }

        if (lcd_text_glyph_index == cpm_glyph &&
            !warmup_active && !show_cpm_row) {
            if (!show_dose_session_stats) {
                ++lcd_text_glyph_index;
                return false;
            }
        }

        if (lcd_text_glyph_index == cpm_glyph &&
            show_dose_session_stats) {
            char stats_text[24];
            const uint8_t stats_length = lcd_format_session_stats(
                stats_text, lcd_dose_stats_mode);
            const uint16_t text_width = (uint16_t)(stats_length *
                (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP) -
                LCD_SMALL_TEXT_GAP);
            const uint8_t x_start = (uint8_t)(2u *
                (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
            const uint8_t y_start = (uint8_t)(LCD_HEIGHT -
                FONT_SMALL_CELL_HEIGHT - 7u);

            x_window[0] = 0u;
            x_window[1] = x_start;
            x_window[2] = 0u;
            x_window[3] = (uint8_t)(x_start + text_width - 1u);
            y_window[0] = 0u;
            y_window[1] = (uint8_t)(LCD_Y_OFFSET + y_start);
            y_window[2] = 0u;
            y_window[3] = (uint8_t)(LCD_Y_OFFSET + y_start +
                FONT_SMALL_CELL_HEIGHT - 1u);

            lcd_text_glyph_pixels = (uint16_t)(text_width *
                FONT_SMALL_CELL_HEIGHT);
            lcd_text_dma_glyph_advance = 1u;
            for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
                for (uint16_t x = 0u; x < text_width; ++x) {
                    const uint8_t column = (uint8_t)(x /
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                    const uint8_t glyph_x = (uint8_t)(x %
                        (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                    const uint16_t pixel = (uint16_t)y * text_width + x;
                    lcd_text_glyph[pixel] = column < stats_length &&
                        glyph_x < FONT_SMALL_CELL_WIDTH &&
                        font_small_pixel(stats_text[column], glyph_x, y) ?
                            LCD_COUNTER_LIGHT_GRAY : LCD_TEXT_BACKGROUND;
                }
            }
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
            return true;
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
                    lcd_text_glyph[(uint16_t)y * glyph_width + x] =
                        font_large_pixel(glyph, x, y) ?
                            (lcd_screen_page == 0u ? lcd_rate_color :
                             LCD_TEXT_FOREGROUND) : LCD_TEXT_BACKGROUND;
                }
            }

            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_command(0x2Au, x_window, sizeof(x_window));
            lcd_command(0x2Bu, y_window, sizeof(y_window));
            lcd_command(0x2Cu, 0, 0u);
            lcd_text_row_commands_queued = true;
            return true;
        }
    }

    {
        const uint8_t glyph_count = LCD_ADC_TEXT_LENGTH;
        const char *small_glyphs = lcd_adc_glyphs;
        uint8_t row;
        uint8_t remaining_glyphs;
        uint8_t row_glyph_count;
        uint16_t row_width;
        uint8_t x_start;
        uint8_t y_start;
        uint8_t x_window[4];
        uint8_t y_window[4];
        uint16_t row_colors[LCD_SMALL_TEXT_COLUMNS];

        if (lcd_text_glyph_index >= glyph_count) {
            lcd_text_active = false;
            return false;
        }

        row = (uint8_t)(lcd_text_glyph_index / LCD_SMALL_TEXT_COLUMNS);
        remaining_glyphs = (uint8_t)(glyph_count -
            row * LCD_SMALL_TEXT_COLUMNS);
        row_glyph_count = remaining_glyphs > LCD_SMALL_TEXT_COLUMNS ?
            LCD_SMALL_TEXT_COLUMNS : remaining_glyphs;
        row_width = (uint16_t)row_glyph_count * FONT_SMALL_CELL_WIDTH +
            (uint16_t)(row_glyph_count - 1u) * LCD_SMALL_TEXT_GAP;
        x_start = 0u;
        y_start = (uint8_t)(LCD_SMALL_TEXT_FIRST_Y + row *
            (FONT_SMALL_CELL_HEIGHT + LCD_SMALL_TEXT_ROW_GAP));
        x_window[0] = 0u;
        x_window[1] = x_start;
        x_window[2] = 0u;
        x_window[3] = (uint8_t)(x_start + row_width - 1u);
        y_window[0] = 0u;
        y_window[1] = (uint8_t)(LCD_Y_OFFSET + y_start);
        y_window[2] = 0u;
        y_window[3] = (uint8_t)(LCD_Y_OFFSET + y_start +
            FONT_SMALL_CELL_HEIGHT - 1u);

        lcd_text_glyph_pixels = (uint16_t)(row_width *
            FONT_SMALL_CELL_HEIGHT);
        lcd_text_dma_glyph_advance = row_glyph_count;
        for (uint8_t column = 0u; column < row_glyph_count; ++column) {
            row_colors[column] = LCD_TEXT_FOREGROUND;
        }
        for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
            for (uint16_t x = 0u; x < row_width; ++x) {
                uint8_t column = (uint8_t)(x /
                    (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                uint8_t glyph_x = (uint8_t)(x %
                    (FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP));
                uint16_t pixel_index = (uint16_t)y * row_width + x;

                if (column < row_glyph_count &&
                    glyph_x < FONT_SMALL_CELL_WIDTH) {
                    char glyph = small_glyphs[(uint8_t)(row *
                        LCD_SMALL_TEXT_COLUMNS + column)];
                    lcd_text_glyph[pixel_index] =
                        font_small_pixel(glyph, glyph_x, y) ?
                            row_colors[column] : LCD_TEXT_BACKGROUND;
                } else {
                    lcd_text_glyph[pixel_index] = LCD_TEXT_BACKGROUND;
                }
            }
        }

        REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u); /* CS low */
        lcd_command(0x2Au, x_window, sizeof(x_window));
        lcd_command(0x2Bu, y_window, sizeof(y_window));
        lcd_command(0x2Cu, 0, 0u); /* RAMWR */
        lcd_text_row_commands_queued = true;
    }
    return true;
}

void board_display_left_short(void)
{
    if (lcd_screen_page >= 2u) {
        board_display_extra_step(false);
        return;
    }
    if (lcd_screen_page == 1u) {
        const uint8_t mode_count = lcd_settings_value_index[9] == 1u ?
            3u : 4u;
        lcd_dose_stats_mode =
            (uint8_t)((lcd_dose_stats_mode + 1u) % mode_count);
        lcd_text_active = false;
        lcd_text_row_commands_queued = false;
        lcd_background_pending = true;
        lcd_cpm_redraw_pending = true;
        return;
    }
    lcd_indication_mode = (uint8_t)((lcd_indication_mode + 1u) % 3u);
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
}

void board_display_right_short(void)
{
    if (lcd_screen_page >= 2u) {
        board_display_extra_step(true);
        return;
    }
    lcd_screen_page = (uint8_t)((lcd_screen_page + 1u) % 2u);
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    if (lcd_screen_page == 0u) {
        lcd_format_rate(lcd_rate_value_thousandths);
        lcd_format_cpm(lcd_current_cpm);
    } else {
        lcd_format_dose(lcd_dose_value_thousandths, lcd_dose_fraction);
    }
    lcd_cpm_redraw_pending = true;
}

bool board_display_search_mode(void)
{
    return lcd_indication_mode == 2u;
}

bool board_display_rate_long_press(void)
{
    if (lcd_screen_page >= 2u || lcd_settings_list_active) return false;

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

void board_display_extra_enter(void)
{
    if (lcd_screen_page >= 2u) {
        return;
    }
    lcd_main_return_page = lcd_screen_page;
    lcd_screen_page = 2u;
    lcd_settings_list_active = false;
    lcd_settings_editing = false;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
}

bool board_display_extra_active(void)
{
    return lcd_screen_page >= 2u;
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
    lcd_settings_list_active = false;
    lcd_settings_editing = false;
    lcd_settings_selected = 0u;
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
    lcd_screen_page = lcd_main_return_page;
    lcd_settings_list_active = false;
    lcd_settings_editing = false;
    lcd_settings_selected = 0u;
    if (lcd_screen_page == 0u) {
        lcd_format_rate(lcd_rate_value_thousandths);
        lcd_format_cpm(lcd_current_cpm);
    } else {
        lcd_format_dose(lcd_dose_value_thousandths, lcd_dose_fraction);
    }
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
}

static void board_display_settings_redraw(void);

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
#define SETTINGS_EEPROM_MAGIC 0xA7u
#define SETTINGS_EEPROM_V2_MAGIC 0xA8u
#define SETTINGS_EEPROM_V3_MAGIC 0xA9u
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

static bool settings_v3_record_read(uint8_t slot, uint8_t *sequence,
                                    uint8_t *lead_zero, uint8_t *unit,
                                    uint8_t *rate_prefix, uint8_t *dose_prefix,
                                    uint8_t *led_brightness,
                                    uint8_t *screen_brightness)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_EEPROM_V3_BASE +
        slot * SETTINGS_EEPROM_V3_SLOT_SIZE);
    *sequence = record[1];
    *lead_zero = record[2];
    *unit = record[3];
    *rate_prefix = record[4];
    *dose_prefix = record[5];
    *led_brightness = record[6];
    *screen_brightness = record[7];
    return record[0] == SETTINGS_EEPROM_V3_MAGIC && *lead_zero < 2u &&
        *unit < 3u && *rate_prefix < (*unit == 1u ? 2u : 4u) &&
        *dose_prefix < (*unit == 1u ? 2u : 4u) &&
        *led_brightness < 9u && *screen_brightness < 9u &&
        record[8] == settings_v3_crc(*sequence, *lead_zero, *unit,
            *rate_prefix, *dose_prefix, *led_brightness, *screen_brightness);
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

static bool settings_eeprom_write_byte(uint8_t offset, uint8_t value)
{
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

#define SETTINGS_CLICKS_EEPROM_BASE 120u
#define SETTINGS_CLICKS_MAGIC 0xB6u
#define SETTINGS_CLICK_TONE_EEPROM_BASE 124u
#define SETTINGS_CLICK_TONE_MAGIC 0xB8u
#define SETTINGS_SOUND_ALARM_EEPROM_BASE 128u
#define SETTINGS_SOUND_ALARM_MAGIC 0xB9u
#define SETTINGS_ALARM_STOP_EEPROM_BASE 132u
#define SETTINGS_ALARM_STOP_MAGIC 0xBAu

static void settings_clicks_load(void)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_CLICKS_EEPROM_BASE);
    if (record[0] == SETTINGS_CLICKS_MAGIC && record[1] < 5u &&
        record[2] == (uint8_t)(0x6Du ^ record[1])) {
        lcd_settings_value_index[1] = record[1];
    }
}

static void settings_clicks_save(void)
{
    const uint8_t value = lcd_settings_value_index[1];
    if (!settings_eeprom_write_byte(SETTINGS_CLICKS_EEPROM_BASE, 0u) ||
        !settings_eeprom_write_byte(SETTINGS_CLICKS_EEPROM_BASE + 1u, value) ||
        !settings_eeprom_write_byte(SETTINGS_CLICKS_EEPROM_BASE + 2u,
            (uint8_t)(0x6Du ^ value))) return;
    (void)settings_eeprom_write_byte(SETTINGS_CLICKS_EEPROM_BASE,
        SETTINGS_CLICKS_MAGIC);
}

static void settings_click_tone_load(void)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_CLICK_TONE_EEPROM_BASE);
    if (record[0] == SETTINGS_CLICK_TONE_MAGIC && record[1] < 3u &&
        record[2] == (uint8_t)(0x73u ^ record[1])) {
        /* Stored value 1 was MODERN; 2 is CLASSIC. Keep its meaning after
         * swapping their positions in the menu. */
        lcd_settings_value_index[20] = record[1] == 1u ? 2u :
            (record[1] == 2u ? 1u : 0u);
    }
}

static void settings_click_tone_save(void)
{
    const uint8_t selected = lcd_settings_value_index[20];
    const uint8_t value = selected == 1u ? 2u :
        (selected == 2u ? 1u : 0u);
    if (!settings_eeprom_write_byte(SETTINGS_CLICK_TONE_EEPROM_BASE, 0u) ||
        !settings_eeprom_write_byte(SETTINGS_CLICK_TONE_EEPROM_BASE + 1u,
            value) ||
        !settings_eeprom_write_byte(SETTINGS_CLICK_TONE_EEPROM_BASE + 2u,
            (uint8_t)(0x73u ^ value))) return;
    (void)settings_eeprom_write_byte(SETTINGS_CLICK_TONE_EEPROM_BASE,
        SETTINGS_CLICK_TONE_MAGIC);
}

static void settings_sound_alarm_load(void)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_SOUND_ALARM_EEPROM_BASE);
    if (record[0] == SETTINGS_SOUND_ALARM_MAGIC && record[1] < 3u &&
        record[2] == (uint8_t)(0x85u ^ record[1])) {
        lcd_settings_value_index[4] = record[1];
    }
}

static void settings_sound_alarm_save(void)
{
    const uint8_t value = lcd_settings_value_index[4];
    if (!settings_eeprom_write_byte(SETTINGS_SOUND_ALARM_EEPROM_BASE, 0u) ||
        !settings_eeprom_write_byte(SETTINGS_SOUND_ALARM_EEPROM_BASE + 1u,
            value) ||
        !settings_eeprom_write_byte(SETTINGS_SOUND_ALARM_EEPROM_BASE + 2u,
            (uint8_t)(0x85u ^ value))) return;
    (void)settings_eeprom_write_byte(SETTINGS_SOUND_ALARM_EEPROM_BASE,
        SETTINGS_SOUND_ALARM_MAGIC);
}

static void settings_alarm_stop_load(void)
{
    const volatile uint8_t *record = (const volatile uint8_t *)(uintptr_t)(
        DATA_EEPROM_BASE + SETTINGS_ALARM_STOP_EEPROM_BASE);
    if (record[0] == SETTINGS_ALARM_STOP_MAGIC && record[1] < 2u &&
        record[2] == (uint8_t)(0xA7u ^ record[1])) {
        lcd_settings_value_index[17] = record[1];
    }
}

static void settings_alarm_stop_save(void)
{
    const uint8_t value = lcd_settings_value_index[17];
    if (!settings_eeprom_write_byte(SETTINGS_ALARM_STOP_EEPROM_BASE, 0u) ||
        !settings_eeprom_write_byte(SETTINGS_ALARM_STOP_EEPROM_BASE + 1u,
            value) ||
        !settings_eeprom_write_byte(SETTINGS_ALARM_STOP_EEPROM_BASE + 2u,
            (uint8_t)(0xA7u ^ value))) return;
    (void)settings_eeprom_write_byte(SETTINGS_ALARM_STOP_EEPROM_BASE,
        SETTINGS_ALARM_STOP_MAGIC);
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
        lcd_settings_value_index[7] = timer1;
    } else if (valid0) {
        lcd_timer_sequence = seq0;
        lcd_settings_value_index[7] = timer0;
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
    const uint8_t timer_index = lcd_settings_value_index[7];
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
    if (*screen_alarm >= 3u || record[4] != (uint8_t)(0x39u ^ *sequence ^
        (*led_mode * 17u) ^ (*screen_alarm * 29u))) return false;
    if (record[0] == SETTINGS_ALARM_V2_EEPROM_MAGIC) return *led_mode < 5u;
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
        lcd_settings_value_index[0] = led1;
        lcd_settings_value_index[2] = alarm1;
    } else if (valid0) {
        lcd_alarm_sequence = seq0;
        lcd_settings_value_index[0] = led0;
        lcd_settings_value_index[2] = alarm0;
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
    const uint8_t led_mode = lcd_settings_value_index[0];
    const uint8_t screen_alarm = lcd_settings_value_index[2];
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
        lcd_settings_value_index[15] = hv1;
    } else if (valid0) {
        lcd_hv_sequence = seq0;
        lcd_settings_value_index[15] = hv0;
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
    const uint8_t hv_mode = lcd_settings_value_index[15];
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
    return record[0] == SETTINGS_LIGHT_ALARM_EEPROM_MAGIC && *mode < 3u &&
        record[3] == (uint8_t)(0x5Du ^ *sequence ^ (*mode * 37u));
}

static void settings_light_alarm_load(void)
{
    uint8_t seq0, mode0, seq1, mode1;
    const bool valid0 = settings_light_alarm_record_read(0u, &seq0, &mode0);
    const bool valid1 = settings_light_alarm_record_read(1u, &seq1, &mode1);

    if (valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0)) {
        lcd_light_alarm_sequence = seq1;
        lcd_settings_value_index[3] = mode1;
    } else if (valid0) {
        lcd_light_alarm_sequence = seq0;
        lcd_settings_value_index[3] = mode0;
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
    const uint8_t mode = lcd_settings_value_index[3];
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
    *sequence = record[1];
    *mode = record[2];
    return record[0] == SETTINGS_DOSE_ALARM_EEPROM_MAGIC && *mode < 11u &&
        record[3] == (uint8_t)(0xA3u ^ *sequence ^ (*mode * 23u));
}

static void settings_dose_alarm_load(void)
{
    uint8_t seq0, mode0, seq1, mode1;
    const bool valid0 = settings_dose_alarm_record_read(0u, &seq0, &mode0);
    const bool valid1 = settings_dose_alarm_record_read(1u, &seq1, &mode1);

    if (valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0)) {
        lcd_dose_alarm_sequence = seq1;
        lcd_settings_value_index[16] = mode1;
    } else if (valid0) {
        lcd_dose_alarm_sequence = seq0;
        lcd_settings_value_index[16] = mode0;
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
    const uint8_t mode = lcd_settings_value_index[16];
    const uint8_t base = (uint8_t)(SETTINGS_DOSE_ALARM_EEPROM_BASE +
        target_slot * SETTINGS_DOSE_ALARM_EEPROM_SLOT_SIZE);

    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), mode) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u),
            (uint8_t)(0xA3u ^ sequence ^ (mode * 23u))) ||
        !settings_eeprom_write_byte(base, SETTINGS_DOSE_ALARM_EEPROM_MAGIC)) {
        return false;
    }
    lcd_dose_alarm_sequence = sequence;
    return true;
}

static void settings_eeprom_load(void)
{
    uint8_t seq0, lead0, unit0, rate0, dose0, led0, screen0;
    uint8_t seq1, lead1, unit1, rate1, dose1, led1, screen1;
    bool valid0 = settings_v3_record_read(0u, &seq0, &lead0, &unit0,
        &rate0, &dose0, &led0, &screen0);
    bool valid1 = settings_v3_record_read(1u, &seq1, &lead1, &unit1,
        &rate1, &dose1, &led1, &screen1);
    uint8_t slot;

    if (valid0 || valid1) {
        slot = valid1 && (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
        if (slot == 0u) {
            lcd_settings_sequence = seq0;
            lcd_settings_value_index[8] = lead0;
            lcd_settings_value_index[9] = unit0;
            lcd_settings_value_index[10] = rate0;
            lcd_settings_value_index[11] = dose0;
            lcd_settings_value_index[5] = led0;
            lcd_settings_value_index[6] = screen0;
        } else {
            lcd_settings_sequence = seq1;
            lcd_settings_value_index[8] = lead1;
            lcd_settings_value_index[9] = unit1;
            lcd_settings_value_index[10] = rate1;
            lcd_settings_value_index[11] = dose1;
            lcd_settings_value_index[5] = led1;
            lcd_settings_value_index[6] = screen1;
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
            lcd_settings_value_index[8] = lead0;
            lcd_settings_value_index[9] = unit0;
            lcd_settings_value_index[10] = rate0;
            lcd_settings_value_index[11] = dose0;
        } else {
            lcd_settings_sequence = seq1;
            lcd_settings_value_index[8] = lead1;
            lcd_settings_value_index[9] = unit1;
            lcd_settings_value_index[10] = rate1;
            lcd_settings_value_index[11] = dose1;
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
            lcd_settings_value_index[8] = lead1;
            lcd_settings_value_index[9] = unit1;
        } else {
            lcd_settings_sequence = seq0;
            lcd_settings_value_index[8] = lead0;
            lcd_settings_value_index[9] = unit0;
        }
        lcd_settings_value_index[10] =
            lcd_settings_value_index[9] == 1u ? 0u : 1u;
        lcd_settings_value_index[11] = lcd_settings_value_index[10];
    }
}

static bool settings_eeprom_save(void)
{
    uint8_t seq0, lead0, unit0, rate0, dose0, led0, screen0;
    uint8_t seq1, lead1, unit1, rate1, dose1, led1, screen1;
    const bool valid0 = settings_v3_record_read(0u, &seq0, &lead0, &unit0,
        &rate0, &dose0, &led0, &screen0);
    const bool valid1 = settings_v3_record_read(1u, &seq1, &lead1, &unit1,
        &rate1, &dose1, &led1, &screen1);
    const uint8_t current_slot = valid1 &&
        (!valid0 || (int8_t)(seq1 - seq0) > 0) ? 1u : 0u;
    const uint8_t target_slot =
        (valid0 || valid1) ? (uint8_t)(current_slot ^ 1u) : 0u;
    const uint8_t sequence = (uint8_t)(lcd_settings_sequence + 1u);
    const uint8_t lead_zero = lcd_settings_value_index[8];
    const uint8_t unit = lcd_settings_value_index[9];
    const uint8_t rate_prefix = lcd_settings_value_index[10];
    const uint8_t dose_prefix = lcd_settings_value_index[11];
    const uint8_t led_brightness = lcd_settings_value_index[5];
    const uint8_t screen_brightness = lcd_settings_value_index[6];
    const uint8_t base = (uint8_t)(SETTINGS_EEPROM_V3_BASE +
        target_slot * SETTINGS_EEPROM_V3_SLOT_SIZE);

    if (!settings_eeprom_write_byte(base, 0u) ||
        !settings_eeprom_write_byte((uint8_t)(base + 1u), sequence) ||
        !settings_eeprom_write_byte((uint8_t)(base + 2u), lead_zero) ||
        !settings_eeprom_write_byte((uint8_t)(base + 3u), unit) ||
        !settings_eeprom_write_byte((uint8_t)(base + 4u), rate_prefix) ||
        !settings_eeprom_write_byte((uint8_t)(base + 5u), dose_prefix) ||
        !settings_eeprom_write_byte((uint8_t)(base + 6u),
            led_brightness) ||
        !settings_eeprom_write_byte((uint8_t)(base + 7u), screen_brightness) ||
        !settings_eeprom_write_byte((uint8_t)(base + 8u),
            settings_v3_crc(sequence, lead_zero, unit, rate_prefix,
                dose_prefix, led_brightness, screen_brightness)) ||
        !settings_eeprom_write_byte(base, SETTINGS_EEPROM_V3_MAGIC)) {
        return false;
    }
    lcd_settings_sequence = sequence;
    return true;
}

bool board_display_settings_list_active(void)
{
    return (lcd_screen_page == 2u || lcd_screen_page == 3u) &&
        lcd_settings_list_active;
}

static void lcd_settings_apply_current(void)
{
    if (lcd_settings_selected == 9u) {
        const uint8_t prefix_default =
            lcd_settings_value_index[9] == 1u ? 0u : 1u;
        lcd_settings_value_index[10] = prefix_default;
        lcd_settings_value_index[11] = prefix_default;
        if (lcd_settings_value_index[9] != 1u &&
            lcd_settings_value_index[12] > 3u) {
            lcd_settings_value_index[12] = 2u;
        }
        if (lcd_settings_value_index[9] == 1u &&
            lcd_dose_stats_mode == 3u) {
            lcd_dose_stats_mode = 0u;
        }
        lcd_format_cpm(lcd_current_cpm);
    }
    if (lcd_settings_selected >= 8u && lcd_settings_selected <= 11u) {
        (void)settings_eeprom_save();
        if (lcd_screen_page == 0u) {
            lcd_format_rate(lcd_rate_value_thousandths);
        } else if (lcd_screen_page == 1u) {
            lcd_format_dose(lcd_dose_value_thousandths, lcd_dose_fraction);
        }
        lcd_cpm_redraw_pending = true;
    }
    if (lcd_settings_selected == 12u) {
        lcd_format_cpm(lcd_current_cpm);
        lcd_cpm_redraw_pending = true;
    }
    if (lcd_settings_selected == 5u || lcd_settings_selected == 6u) {
        (void)settings_eeprom_save();
        lcd_apply_brightness_settings();
    }
    if (lcd_settings_selected == 7u) settings_timer_save();
    if (lcd_settings_selected == 1u) settings_clicks_save();
    if (lcd_settings_selected == 4u) settings_sound_alarm_save();
    if (lcd_settings_selected == 20u) settings_click_tone_save();
    if (lcd_settings_selected == 17u) settings_alarm_stop_save();
    if (lcd_settings_selected == 0u || lcd_settings_selected == 2u) {
        (void)settings_alarm_save();
    }
    if (lcd_settings_selected == 3u) {
        (void)settings_light_alarm_save();
    }
    if (lcd_settings_selected == 16u) {
        (void)settings_dose_alarm_save();
    }
    if (lcd_settings_selected == 15u) {
        board_hv_set_enabled(lcd_settings_value_index[15] == 0u);
        (void)settings_hv_save();
        if (lcd_settings_value_index[15] == 0u) {
            lcd_rate_value_thousandths = 0u;
            lcd_current_cpm = 0u;
            lcd_format_rate(0u);
            lcd_format_cpm(0u);
        }
        lcd_text_active = false;
        lcd_text_row_commands_queued = false;
        lcd_background_pending = true;
        lcd_cpm_redraw_pending = true;
    }
}

static bool settings_reset_all(void)
{
    static const uint8_t record_magic_offsets[] = {
        0u, 5u, 16u, 23u, 32u, 41u, 64u, 68u, 80u, 85u,
        96u, 100u, 104u, 108u, 112u, 116u, 120u, 124u, 128u, 132u
    };

    for (uint8_t i = 0u;
         i < sizeof(record_magic_offsets) / sizeof(record_magic_offsets[0]);
         ++i) {
        if (!settings_eeprom_write_byte(record_magic_offsets[i], 0u)) {
            return false;
        }
    }
    return true;
}

uint32_t board_display_screen_timeout_ms(void)
{
    static const uint8_t timeout_seconds[] = { 1u, 5u, 10u, 30u, 60u, 90u };
    if (lcd_indication_mode == 0u) return 1000u;
    return (uint32_t)timeout_seconds[lcd_settings_value_index[7]] * 1000u;
}

bool board_display_led_flash_enabled(bool screen_on)
{
    if (lcd_indication_mode == 0u) return false;
    if (lcd_indication_mode == 2u) return true;
    const uint8_t mode = lcd_settings_value_index[0];
    if (mode == 1u) return screen_on;
    if (mode == 2u) return true;
    if (mode == 3u) return lcd_rate_color == LCD_COUNTER_YELLOW ||
        lcd_rate_color == LCD_COUNTER_RED;
    if (mode == 4u) return lcd_rate_color == LCD_COUNTER_RED;
    return false;
}

bool board_display_click_sound_enabled(bool screen_on)
{
    if (lcd_indication_mode == 0u) return false;
    const uint8_t mode = lcd_settings_value_index[1];
    if (mode == 1u) return screen_on;
    if (mode == 2u) return true;
    if (mode == 3u) return lcd_rate_color == LCD_COUNTER_YELLOW ||
        lcd_rate_color == LCD_COUNTER_RED;
    if (mode == 4u) return lcd_rate_color == LCD_COUNTER_RED;
    return false;
}

static void board_click_sound_stop(void)
{
    REG32(TIM2_BASE + TIM_CCER) &= ~(1u << 12u);
    REG32(TIM2_BASE + TIM_CCR4) = 0u;
    REG32(TIM2_BASE + TIM_PSC) = 0u;
    REG32(TIM2_BASE + TIM_ARR) = PWM_PERIOD - 1u;
    board_rgb_apply();
    REG32(TIM2_BASE + TIM_EGR) = 1u;
    REG32(GPIOA_BASE + GPIO_MODER) &= ~(3u << 6u);
    pin_input_with_pull(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN);
    click_sound_active = false;
    click_sound_stage = 0u;
}

void board_click_sound_start(void)
{
    uint32_t compare = ((uint32_t)rgb_brightness_percent * 311u) / 100u;
    if (compare > 310u) compare = 310u;
    REG32(GPIOA_BASE + GPIO_PUPDR) &= ~(3u << 6u);
    REG32(GPIOA_BASE + GPIO_OTYPER) &= ~(1u << 3u);
    REG32(GPIOA_BASE + GPIO_OSPEEDR) &= ~(3u << 6u);
    REG32(GPIOA_BASE + GPIO_AFRL) =
        (REG32(GPIOA_BASE + GPIO_AFRL) & ~(0xFu << 12u)) | (2u << 12u);
    REG32(GPIOA_BASE + GPIO_MODER) =
        (REG32(GPIOA_BASE + GPIO_MODER) & ~(3u << 6u)) | (2u << 6u);

    sound_alarm_mode = false;
    click_sound_profile = lcd_settings_value_index[20];
    click_sound_stage = 0u;
    REG32(TIM2_BASE + TIM_PSC) = 22u;
    REG32(TIM2_BASE + TIM_ARR) = 310u;
    REG32(TIM2_BASE + TIM_CCR1) = (rgb_channels & 1u) ?
        (compare * rgb_level[0]) / 255u : 0u;
    REG32(TIM2_BASE + TIM_CCR2) = (rgb_channels & 2u) ?
        (compare * rgb_level[1]) / 255u : 0u;
    REG32(TIM2_BASE + TIM_CCMR2) =
        (REG32(TIM2_BASE + TIM_CCMR2) & ~(0xFFu << 8u)) |
        (6u << 12u) | (1u << 11u); /* CH4 PWM1, preload enabled. */
    REG32(TIM2_BASE + TIM_CCR4) = 50u;
    REG32(TIM2_BASE + TIM_EGR) = 1u;
    REG32(TIM2_BASE + TIM_CNT) = 0u;
    REG32(TIM2_BASE + TIM_CCER) |= 1u << 12u;
    click_sound_deadline = board_millis +
        (click_sound_profile == 2u ? 4u :
            (click_sound_profile == 1u ? 2u : 38u));
    click_sound_active = true;
}

void board_sound_alarm_set(bool active)
{
    uint32_t compare = ((uint32_t)rgb_brightness_percent * 311u) / 100u;
    if (compare > 310u) compare = 310u;
    if (!active) {
        if (sound_alarm_mode) board_click_sound_stop();
        sound_alarm_mode = false;
        return;
    }
    if (sound_alarm_mode) return;

    sound_alarm_mode = true;
    click_sound_active = true;
    click_sound_profile = lcd_settings_value_index[20];
    sound_alarm_phase = 0u;
    REG32(GPIOA_BASE + GPIO_PUPDR) &= ~(3u << 6u);
    REG32(GPIOA_BASE + GPIO_OTYPER) &= ~(1u << 3u);
    REG32(GPIOA_BASE + GPIO_OSPEEDR) &= ~(3u << 6u);
    REG32(GPIOA_BASE + GPIO_AFRL) =
        (REG32(GPIOA_BASE + GPIO_AFRL) & ~(0xFu << 12u)) | (2u << 12u);
    REG32(GPIOA_BASE + GPIO_MODER) =
        (REG32(GPIOA_BASE + GPIO_MODER) & ~(3u << 6u)) | (2u << 6u);
    REG32(TIM2_BASE + TIM_ARR) = 310u;
    REG32(TIM2_BASE + TIM_CCR1) = (rgb_channels & 1u) ?
        (compare * rgb_level[0]) / 255u : 0u;
    REG32(TIM2_BASE + TIM_CCR2) = (rgb_channels & 2u) ?
        (compare * rgb_level[1]) / 255u : 0u;
    REG32(TIM2_BASE + TIM_CCMR2) =
        (REG32(TIM2_BASE + TIM_CCMR2) & ~(0xFFu << 8u)) |
        (6u << 12u) | (1u << 11u);
    REG32(TIM2_BASE + TIM_CCR4) = 50u;
    REG32(TIM2_BASE + TIM_PSC) = click_sound_profile == 1u ? 31u : 22u;
    REG32(TIM2_BASE + TIM_EGR) = 1u;
    REG32(TIM2_BASE + TIM_CNT) = 0u;
    REG32(TIM2_BASE + TIM_CCER) |= 1u << 12u;
    click_sound_deadline = board_millis +
        (click_sound_profile == 2u ? 120u :
            (click_sound_profile == 1u ? 240u : 400u));
}

void board_click_sound_service(void)
{
    if (sound_alarm_mode &&
        (int32_t)(board_millis - click_sound_deadline) >= 0) {
        if (click_sound_profile == 2u) {
            sound_alarm_phase = (uint8_t)((sound_alarm_phase + 1u) % 4u);
            if (sound_alarm_phase == 1u) {
                REG32(TIM2_BASE + TIM_CCR4) = 0u;
                click_sound_deadline += 50u;
            } else {
                REG32(TIM2_BASE + TIM_CCR4) = 50u;
                REG32(TIM2_BASE + TIM_PSC) =
                    (sound_alarm_phase == 0u || sound_alarm_phase == 3u) ?
                    31u : 18u;
                click_sound_deadline += 120u;
            }
        } else {
            sound_alarm_phase ^= 1u;
            REG32(TIM2_BASE + TIM_PSC) =
                (sound_alarm_phase == 0u) ?
                (click_sound_profile == 1u ? 31u : 22u) :
                (click_sound_profile == 1u ? 22u : 31u);
            click_sound_deadline += click_sound_profile == 1u ? 240u : 400u;
        }
        REG32(TIM2_BASE + TIM_EGR) = 1u;
        REG32(TIM2_BASE + TIM_CNT) = 0u;
    } else if (click_sound_active && !sound_alarm_mode &&
        (int32_t)(board_millis - click_sound_deadline) >= 0) {
        if (click_sound_profile != 2u || click_sound_stage >= 2u) {
            board_click_sound_stop();
        } else if (click_sound_stage == 0u) {
            REG32(TIM2_BASE + TIM_CCR4) = 0u;
            REG32(TIM2_BASE + TIM_EGR) = 1u;
            click_sound_stage = 1u;
            click_sound_deadline += 2u;
        } else {
            REG32(TIM2_BASE + TIM_PSC) = 31u;
            REG32(TIM2_BASE + TIM_CCR4) = 50u;
            REG32(TIM2_BASE + TIM_EGR) = 1u;
            click_sound_stage = 2u;
            click_sound_deadline += 8u;
        }
    }
}

bool board_display_screen_alarm_active(void)
{
    const uint8_t mode = lcd_settings_value_index[2];
    if (lcd_indication_mode == 0u || lcd_settings_value_index[15] != 0u)
        return false;
    if (mode == 0u) return false;
    const bool rate_alarm = mode == 1u ?
        (lcd_rate_color == LCD_COUNTER_YELLOW ||
         lcd_rate_color == LCD_COUNTER_RED) :
        (mode == 2u && lcd_rate_color == LCD_COUNTER_RED);
    return rate_alarm || board_display_dose_alarm_active();
}

bool board_display_light_alarm_active(void)
{
    const uint8_t mode = lcd_settings_value_index[3];
    if (lcd_indication_mode == 0u || lcd_settings_value_index[15] != 0u)
        return false;
    if (mode == 0u) return false;
    const bool rate_alarm = mode == 1u ?
        (lcd_rate_color == LCD_COUNTER_YELLOW ||
         lcd_rate_color == LCD_COUNTER_RED) :
        (mode == 2u && lcd_rate_color == LCD_COUNTER_RED);
    return rate_alarm || board_display_dose_alarm_active();
}

bool board_display_dose_alarm_active(void)
{
    static const uint16_t fixed_threshold_micro_sv[] = {
        0u, 0u, 0u, 5u, 7u, 10u, 30u, 50u, 100u, 500u, 1000u
    };
    const uint8_t mode = lcd_settings_value_index[16];
    const uint64_t actual_nsv_x3600 =
        (uint64_t)lcd_dose_value_thousandths * 3600u + lcd_dose_fraction;
    uint64_t threshold_nsv_x3600;

    if (mode == 0u) return false;
    if (mode == 1u || mode == 2u) {
        if (lcd_dose_measurement_seconds == 0u) return false;
        threshold_nsv_x3600 = (uint64_t)(mode == 1u ? 400u : 900u) *
            lcd_dose_measurement_seconds;
    } else {
        threshold_nsv_x3600 =
            (uint64_t)fixed_threshold_micro_sv[mode] * 1000u * 3600u;
    }
    return actual_nsv_x3600 >= threshold_nsv_x3600;
}

bool board_display_sound_alarm_active(void)
{
    const uint8_t mode = lcd_settings_value_index[4];
    bool rate_alarm;

    if (lcd_indication_mode == 0u || mode == 0u ||
        lcd_settings_value_index[15] != 0u) return false;
    rate_alarm = mode == 1u ?
        (lcd_rate_color == LCD_COUNTER_YELLOW ||
         lcd_rate_color == LCD_COUNTER_RED) :
        lcd_rate_color == LCD_COUNTER_RED;
    return rate_alarm || board_display_dose_alarm_active();
}

bool board_display_sound_alarm_stop_on_clear(void)
{
    return lcd_indication_mode == 0u || lcd_settings_value_index[17] == 0u ||
        lcd_settings_value_index[4] == 0u ||
        lcd_settings_value_index[15] != 0u;
}

void board_display_settings_list_activate(void)
{
    if ((lcd_screen_page != 2u && lcd_screen_page != 3u) ||
        lcd_settings_list_active) return;
    lcd_settings_list_active = true;
    if (lcd_screen_page == 2u) {
        lcd_settings_selected = 0u;
        lcd_settings_editing = false;
        rtc_get_time_edit_base(&lcd_time_draft);
        lcd_time_draft_ready = true;
        lcd_time_save_failed = false;
        lcd_time_save_succeeded = false;
        lcd_time_save_status_deadline = 0u;
        lcd_background_pending = true;
    }
    board_display_settings_redraw();
}

static void board_display_settings_redraw(void)
{
    if (lcd_screen_page == 2u) {
        lcd_time_redraw_pending = true;
    } else {
        lcd_settings_redraw_pending = true;
    }
}

void board_display_settings_move(bool forward)
{
    if (!board_display_settings_list_active()) return;
    if (lcd_screen_page == 2u) {
        if (lcd_settings_editing) {
            uint16_t minimum;
            uint16_t maximum;
            switch (lcd_settings_selected) {
            case 0u: minimum = 2000u; maximum = 2099u; break;
            case 1u: minimum = 1u; maximum = 12u; break;
            case 2u: minimum = 1u;
                maximum = rtc_days_in_month(lcd_time_draft.year,
                                             lcd_time_draft.month);
                break;
            case 3u: minimum = 0u; maximum = 23u; break;
            default: minimum = 0u; maximum = 59u; break;
            }
            if (forward) {
                lcd_time_edit_value = lcd_time_edit_value >= maximum ?
                    minimum : (uint16_t)(lcd_time_edit_value + 1u);
            } else {
                lcd_time_edit_value = lcd_time_edit_value <= minimum ?
                    maximum : (uint16_t)(lcd_time_edit_value - 1u);
            }
            board_display_settings_redraw();
            return;
        }
        lcd_settings_selected = forward ?
            (uint8_t)((lcd_settings_selected + 1u) % 6u) :
            (uint8_t)((lcd_settings_selected + 5u) % 6u);
        board_display_settings_redraw();
        return;
    }
    if (lcd_settings_editing) {
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
    if (lcd_screen_page == 2u) {
        if (lcd_settings_selected == 5u && !lcd_settings_editing) {
            if (lcd_time_draft_ready &&
                rtc_write_calendar(&lcd_time_draft, true)) {
                lcd_time_save_failed = false;
                lcd_time_save_succeeded = true;
                lcd_time_redraw_pending = true;
            } else {
                lcd_time_save_failed = true;
                lcd_time_save_succeeded = false;
                if (!lcd_time_draft_ready) lcd_time_save_error = 4u;
            }
            lcd_time_save_status_deadline = board_millis + 3000u;
            board_display_settings_redraw();
            return;
        }
        if (!lcd_settings_editing) {
            lcd_time_save_failed = false;
            lcd_time_save_succeeded = false;
            switch (lcd_settings_selected) {
            case 0u: lcd_time_edit_value = lcd_time_draft.year; break;
            case 1u: lcd_time_edit_value = lcd_time_draft.month; break;
            case 2u: lcd_time_edit_value = lcd_time_draft.day; break;
            case 3u: lcd_time_edit_value = lcd_time_draft.hour; break;
            default: lcd_time_edit_value = lcd_time_draft.minute; break;
            }
            lcd_time_edit_original = lcd_time_edit_value;
            lcd_settings_editing = true;
        } else {
            switch (lcd_settings_selected) {
            case 0u: lcd_time_draft.year = lcd_time_edit_value; break;
            case 1u: lcd_time_draft.month = (uint8_t)lcd_time_edit_value; break;
            case 2u: lcd_time_draft.day = (uint8_t)lcd_time_edit_value; break;
            case 3u: lcd_time_draft.hour = (uint8_t)lcd_time_edit_value; break;
            default: lcd_time_draft.minute = (uint8_t)lcd_time_edit_value; break;
            }
            if (lcd_time_draft.day > rtc_days_in_month(lcd_time_draft.year,
                                                       lcd_time_draft.month)) {
                lcd_time_draft.day = rtc_days_in_month(lcd_time_draft.year,
                                                       lcd_time_draft.month);
            }
            lcd_settings_editing = false;
            lcd_time_save_failed = false;
            lcd_time_save_succeeded = false;
        }
        board_display_settings_redraw();
        return;
    }
    if (lcd_settings_selected == 14u) {
        if (settings_reset_all()) board_reboot();
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
    if (lcd_settings_value_count(lcd_settings_selected) > 3u) {
        if (!lcd_settings_editing) {
            lcd_settings_edit_value =
                lcd_settings_value_index[lcd_settings_selected];
            lcd_settings_editing = true;
        } else {
            lcd_settings_value_index[lcd_settings_selected] =
                lcd_settings_edit_value;
            lcd_settings_editing = false;
            lcd_settings_apply_current();
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
    if (lcd_screen_page == 2u) {
        if (lcd_settings_editing) {
            lcd_time_edit_value = lcd_time_edit_original;
            lcd_settings_editing = false;
            board_display_settings_redraw();
            return;
        }
        lcd_settings_list_active = false;
        lcd_settings_editing = false;
        lcd_time_draft_ready = false;
        lcd_background_pending = true;
        board_display_settings_redraw();
        return;
    }
    if (lcd_settings_editing) {
        lcd_settings_editing = false;
        board_display_settings_redraw();
        return;
    }
    lcd_settings_list_active = false;
    board_display_settings_redraw();
}

void board_display_set_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction,
                            uint32_t measurement_seconds)
{
    static uint32_t previous_dose_thousandths = UINT32_MAX;
    static uint32_t previous_dose_fraction = UINT32_MAX;

    lcd_dose_value_thousandths = dose_thousandths;
    lcd_dose_fraction = dose_fraction;
    lcd_dose_measurement_seconds = measurement_seconds;
    if (dose_thousandths != previous_dose_thousandths ||
        dose_fraction != previous_dose_fraction) {
        previous_dose_thousandths = dose_thousandths;
        previous_dose_fraction = dose_fraction;
        if (lcd_screen_page == 1u) {
            lcd_format_dose(dose_thousandths, dose_fraction);
            lcd_cpm_redraw_pending = true;
        }
    }
}

void board_display_set_session_rates(uint32_t average_rate_thousandths,
                                     uint32_t maximum_rate_thousandths,
                                     uint32_t average_cpm,
                                     uint32_t maximum_cpm,
                                     uint32_t session_count_total)
{
    if (lcd_session_average_rate_thousandths == average_rate_thousandths &&
        lcd_session_maximum_rate_thousandths == maximum_rate_thousandths &&
        lcd_session_average_cpm == average_cpm &&
        lcd_session_maximum_cpm == maximum_cpm &&
        lcd_session_count_total == session_count_total) {
        return;
    }
    lcd_session_average_rate_thousandths = average_rate_thousandths;
    lcd_session_maximum_rate_thousandths = maximum_rate_thousandths;
    lcd_session_average_cpm = average_cpm;
    lcd_session_maximum_cpm = maximum_cpm;
    lcd_session_count_total = session_count_total;
    if (lcd_screen_page == 1u && lcd_dose_stats_mode != 0u) {
        lcd_cpm_redraw_pending = true;
    }
}

void board_display_set_count(uint32_t count)
{
    if (lcd_count_total == count) return;
    lcd_count_total = count;
    if (lcd_settings_value_index[9] == 1u && lcd_screen_page == 1u) {
        lcd_format_dose(lcd_dose_value_thousandths, lcd_dose_fraction);
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
    uint16_t rate_color = LCD_COUNTER_PURPLE;

    lcd_rate_value_thousandths = rate_thousandths;
    lcd_warmup_pulses = pulses_since_start;
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
        } else if (rate_color == LCD_COUNTER_PURPLE) {
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
        if (lcd_settings_value_index[9] == 1u && lcd_screen_page == 0u) {
            lcd_format_rate(rate_thousandths);
        }
        changed = true;
    }
    if (cpm != 0u && rate_thousandths != previous_rate_thousandths) {
        previous_rate_thousandths = rate_thousandths;
        if (lcd_screen_page == 0u) {
            lcd_format_rate(rate_thousandths);
        }
        changed = true;
    }
    if (changed && lcd_ready) {
        lcd_cpm_redraw_pending = true;
    }
}

uint8_t board_display_rate_led_mask(void)
{
    return lcd_rate_led_mask;
}

void board_display_set_rate_led(void)
{
    if (lcd_rate_color == LCD_COUNTER_PURPLE) {
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

static void lcd_apply_brightness_settings(void)
{
    static const uint8_t levels[] = {
        0u, 1u, 3u, 5u, 10u, 25u, 50u, 80u, 100u
    };
    const uint8_t led_setting = lcd_settings_value_index[5];
    const uint8_t screen_setting = lcd_settings_value_index[6];
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
        apply_screen = lcd_solar_sample_ready;
        screen_percent = (uint8_t)lcd_displayed_solar_percent;
        if (screen_percent < 1u) screen_percent = 1u;
        if (lcd_adc_sun_raw >= 10u && screen_percent < 10u) {
            screen_percent = 10u;
        }
    } else {
        screen_percent = levels[screen_setting];
    }
    if (apply_led && rgb_brightness_percent != led_percent) {
        board_rgb_set_brightness(led_percent);
    }
    if (apply_screen && backlight_brightness_percent != screen_percent) {
        board_lcd_backlight_set_brightness(screen_percent);
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

    if (channel2 != lcd_adc_battery_raw || channel8 != lcd_adc_usb_raw ||
        channel9 != lcd_adc_sun_raw) {
        changed = true;
    }
    lcd_adc_battery_raw = channel2;
    lcd_adc_sun_raw = channel9;
    lcd_adc_usb_raw = channel8;
    if (channel2 <= 4095u) {
        battery_percent = lcd_battery_percent(channel2);
        if (!lcd_battery_percent_initialized) {
            /* Give the display a useful initial value; debounce later changes. */
            lcd_displayed_battery_percent = battery_percent;
            lcd_battery_percent_candidate = battery_percent;
            lcd_battery_percent_candidate_since_ms = board_millis;
            lcd_battery_percent_initialized = true;
            lcd_format_percent(battery_percent, 13u);
            changed = true;
        } else if (battery_percent != lcd_battery_percent_candidate) {
            lcd_battery_percent_candidate = battery_percent;
            lcd_battery_percent_candidate_since_ms = board_millis;
        } else if (battery_percent != lcd_displayed_battery_percent &&
                   (uint32_t)(board_millis -
                       lcd_battery_percent_candidate_since_ms) >=
                           LCD_BATTERY_PERCENT_STABLE_MS) {
            lcd_displayed_battery_percent = battery_percent;
            lcd_format_percent(battery_percent, 13u);
            changed = true;
        }
    }
    if (channel9 <= 4095u) {
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
        lcd_cpm_redraw_pending = true;
        lcd_debug_redraw_pending = true;
    }
}

static void lcd_text_render_service(void)
{
    if (lcd_background_pending || lcd_fill_start_pending || lcd_fill_active ||
        lcd_dma_active || !lcd_queue_drained()) {
        return;
    }
    if (!lcd_text_active && lcd_cpm_redraw_pending) {
        lcd_text_active = true;
        lcd_text_glyph_index = 0u;
        lcd_text_row_commands_queued = false;
        lcd_cpm_redraw_pending = false;
    }
    if (!lcd_text_active && lcd_screen_page == 3u &&
        lcd_settings_redraw_pending) {
        lcd_text_active = true;
        lcd_text_glyph_index = 0u;
        lcd_text_row_commands_queued = false;
        lcd_settings_redraw_pending = false;
    }
    if (lcd_screen_page == 2u && lcd_time_redraw_pending &&
        !lcd_text_active) {
        lcd_text_active = true;
        lcd_text_glyph_index = 0u;
        lcd_text_row_commands_queued = false;
        lcd_time_redraw_pending = false;
    }
    if (lcd_screen_page == 4u && lcd_debug_redraw_pending &&
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
    lcd_check_output_level(GPIOB_BASE, 11u, true);
    lcd_spi_dma_start(lcd_text_glyph, lcd_text_glyph_pixels, true, true,
                      lcd_text_dma_glyph_advance);
}

void board_display_service(void)
{
    if (!lcd_started) {
        return;
    }
    lcd_spi_pump();
    if (lcd_init_state == LCD_INIT_FAILED || !lcd_spi_ready) {
        return;
    }
    if ((lcd_time_save_failed || lcd_time_save_succeeded) &&
        deadline_reached(lcd_time_save_status_deadline)) {
        lcd_time_save_failed = false;
        lcd_time_save_succeeded = false;
        if (lcd_screen_page == 2u && lcd_settings_list_active &&
            !lcd_settings_editing && lcd_settings_selected == 5u) {
            board_display_settings_redraw();
        }
    }

    switch (lcd_init_state) {
    case LCD_INIT_BOARD_ENABLE_DELAY:
        if (deadline_reached(lcd_init_deadline)) {
            /* Stock LCD_Init asserts CS only after the PB7 settle delay. */
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_check_output_level(GPIOA_BASE, 11u, false);
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 8u);
            lcd_check_output_level(GPIOA_BASE, 8u, false);
            lcd_init_deadline = board_millis + 5u;
            lcd_init_state = LCD_INIT_RESET_LOW;
        }
        return;
    case LCD_INIT_RESET_LOW:
        if (deadline_reached(lcd_init_deadline)) {
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 8u;
            lcd_check_output_level(GPIOA_BASE, 8u, true);
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
            lcd_check_output_level(GPIOA_BASE, 11u, true);
            /* Stock main() sends INVOFF as a separate CS transaction. */
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
            lcd_check_output_level(GPIOA_BASE, 11u, false);
            lcd_command(0x20u, 0, 0u);
            lcd_init_state = LCD_INIT_POST_INIT_INVOFF_TX;
        }
        break;
    case LCD_INIT_POST_INIT_INVOFF_TX:
        if (lcd_queue_drained()) {
            REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u; /* CS high */
            lcd_check_output_level(GPIOA_BASE, 11u, true);
            lcd_ready = true;
            lcd_init_state = LCD_INIT_READY;
            /* Clear the panel before drawing the large-glyph test screen. */
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
    if (lcd_screen_page == 2u) {
        rtc_calendar_t calendar;
        uint32_t minute_key = board_uptime_seconds / 60u;
        if (rtc_time_valid && rtc_read_calendar(&calendar)) {
            minute_key = (uint32_t)calendar.year * 1000000u +
                (uint32_t)calendar.month * 10000u +
                (uint32_t)calendar.day * 100u +
                (uint32_t)calendar.hour * 60u + calendar.minute;
        }
        if (minute_key != lcd_time_displayed_minute) {
            lcd_time_displayed_minute = minute_key;
            lcd_time_redraw_pending = true;
        }
    } else if (lcd_screen_page < 2u) {
        rtc_calendar_t calendar;
        uint32_t minute_key = board_uptime_seconds / 60u;
        if (rtc_time_valid && rtc_read_calendar(&calendar)) {
            minute_key = (uint32_t)calendar.year * 1000000u +
                (uint32_t)calendar.month * 10000u +
                (uint32_t)calendar.day * 100u +
                (uint32_t)calendar.hour * 60u + calendar.minute;
        }
        if (minute_key != lcd_time_displayed_minute) {
            lcd_time_displayed_minute = minute_key;
            lcd_cpm_redraw_pending = true;
        }
    } else if (lcd_screen_page == 4u) {
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
}

bool board_display_busy(void)
{
    if (!lcd_started) {
        return false;
    }
    if (lcd_spi_error || lcd_control_error) {
        return false;
    }
    return !lcd_ready || !lcd_queue_drained() || lcd_background_pending ||
           lcd_fill_start_pending ||
           lcd_fill_active || lcd_text_active;
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
    settings_clicks_load();
    settings_click_tone_load();
    settings_sound_alarm_load();
    settings_alarm_stop_load();
    settings_timer_load();
    settings_alarm_load();
    settings_hv_load();
    settings_light_alarm_load();
    settings_dose_alarm_load();
    lcd_apply_brightness_settings();
    /* Replace static startup labels with the loaded unit/prefix choices
     * before the LCD's initial background fill and first text render. */
    lcd_format_rate(lcd_rate_value_thousandths);
    lcd_format_dose(lcd_dose_value_thousandths, lcd_dose_fraction);
    lcd_cpm_redraw_pending = true;
    lcd_started = true;
    lcd_configure();
    /* RTC, saved configuration and display are ready before enabling HV. */
    board_hv_set_enabled(lcd_settings_value_index[15] == 0u);
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
    const uint32_t exti_port_shift = (DETECTOR_PIN - 4u) * 4u;

    /* Stock HEX dispatches EXTI lines 4, 6 and 9. Lines 4/9 are the known
     * buttons; PA6 is the confirmed blue LED output, so PB6 is the remaining
     * detector-input candidate. Route PB6 to EXTI6 and count its falling edge. */
    pin_input_with_pull(DETECTOR_GPIO_BASE, DETECTOR_PIN);
    RCC_APB2ENR |= 1u; /* SYSCFG clock */
    (void)RCC_APB2ENR;
    *exticr2 = (*exticr2 & ~(0xFu << exti_port_shift)) |
        (1u << exti_port_shift); /* EXTI6 source = GPIOB */
    EXTI_IMR &= ~DETECTOR_EXTI_MASK;
    EXTI_RTSR &= ~DETECTOR_EXTI_MASK;
    EXTI_FTSR |= DETECTOR_EXTI_MASK;
    EXTI_PR = DETECTOR_EXTI_MASK;
    NVIC_ISER = 1u << 7u; /* EXTI4_15_IRQn */
    EXTI_IMR |= DETECTOR_EXTI_MASK;
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

    ADC_ISR = 1u; /* Clear ADRDY, if set. */
    ADC_CR = 1u; /* ADEN */
    timeout = 1000000u;
    while ((ADC_ISR & 1u) == 0u && --timeout != 0u) { }
    adc_ready = timeout != 0u;
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + 10u);
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

void board_adc_read_all(uint16_t *channel2, uint16_t *channel8,
                        uint16_t *channel9)
{
    uint16_t values[3] = { UINT16_MAX, UINT16_MAX, UINT16_MAX };

    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 10u;
    adc_wait_ms(5u);
    if (adc_ready) {
        values[0] = board_adc_read(2u);
        values[1] = board_adc_read(8u);
        values[2] = board_adc_read(9u);
    }
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + 10u);
    adc_wait_ms(1u);

    *channel2 = values[0];
    *channel8 = values[1];
    *channel9 = values[2];
}

void EXTI4_15_IRQHandler(void)
{
    const uint32_t pending = EXTI_PR;

    if ((pending & DETECTOR_EXTI_MASK) != 0u) {
        EXTI_PR = DETECTOR_EXTI_MASK;
        if (board_hv_enabled && detector_pulse_count < 999999u) {
            ++detector_pulse_count;
        }
    }
    if ((pending & (1u << BOARD_BUTTON_LEFT_PIN)) != 0u) {
        EXTI_PR = 1u << BOARD_BUTTON_LEFT_PIN;
        power_off_wakeup = true;
    }
}

void board_reboot(void)
{
    __asm volatile ("dsb");
    SCB_AIRCR = 0x05FA0004u;
    __asm volatile ("dsb\n\tisb");
    for (;;) {
        __asm volatile ("wfi");
    }
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
    board_reboot();
}

static bool button_pressed(uint32_t base, uint32_t pin)
{
    uint32_t level = gpio_read(base, pin);
    return BOARD_BUTTON_ACTIVE_LOW ? (level == 0u) : (level != 0u);
}

static uint8_t debounce(button_debounce_t *button, bool raw_pressed,
                        uint8_t press_event, uint8_t short_event,
                        uint8_t long_event, uint8_t release_event)
{
    uint8_t events = 0u;
    uint8_t raw = raw_pressed ? 1u : 0u;

    if (raw != button->candidate) {
        button->candidate = raw;
        button->count = 1u;
    } else if (button->count < 3u) {
        ++button->count;
    }

    if (button->count >= 3u && button->stable != button->candidate) {
        button->stable = button->candidate;
        if (button->stable != 0u) {
            events |= press_event;
            button->held_ticks = 0u;
            button->long_reported = 0u;
            button->repeat_ticks = 0u;
        } else {
            events |= release_event;
            if (button->long_reported == 0u) {
                events |= short_event;
            }
            button->held_ticks = 0u;
            button->long_reported = 0u;
            button->repeat_ticks = 0u;
        }
    }

    if (button->stable != 0u) {
        if (button->held_ticks < 0xFFFFu) {
            ++button->held_ticks;
        }
        if (button->held_ticks >= LONG_PRESS_TICKS &&
            button->long_reported == 0u) {
            button->long_reported = 1u;
            button->repeat_ticks = 0u;
            events |= long_event;
        } else if (button->long_reported != 0u) {
            uint8_t interval_ticks = 10u;

            /* Accelerate geometrically with hold duration: 10, 7, 5, 3,
             * then 2 samples per repeat (100 ms down to 20 ms). */
            if (button->held_ticks >= LONG_PRESS_TICKS + 400u) {
                interval_ticks = 2u;
            } else if (button->held_ticks >= LONG_PRESS_TICKS + 300u) {
                interval_ticks = 3u;
            } else if (button->held_ticks >= LONG_PRESS_TICKS + 200u) {
                interval_ticks = 5u;
            } else if (button->held_ticks >= LONG_PRESS_TICKS + 100u) {
                interval_ticks = 7u;
            }
            if (++button->repeat_ticks >= interval_ticks) {
                button->repeat_ticks = 0u;
                events |= long_event;
            }
        }
    }
    return events;
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
    REG32(base + TIM_CR1) = (1u << 7) | 1u; /* ARPE, counter enable */
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

    /* Establish a baseline so a button held at reset does not advance a color. */
    left_button.stable = left_button.candidate =
        button_pressed(GPIOB_BASE, BOARD_BUTTON_LEFT_PIN) ? 1u : 0u;
    right_button.stable = right_button.candidate =
        button_pressed(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN) ? 1u : 0u;
    left_button.count = right_button.count = 0u;
    /* Ignore a button already held at reset until it is released. */
    left_button.long_reported = left_button.stable;
    right_button.long_reported = right_button.stable;
    left_button.held_ticks = right_button.held_ticks = 0u;

    SYST_RVR = 32000u - 1u; /* 1 ms at 32 MHz SYSCLK */
    SYST_CVR = 0u;
    SYST_CSR = 7u; /* processor clock, interrupt, enable */
}

uint8_t board_buttons_sample_10ms(void)
{
    uint8_t events = 0u;
    /* Right advances through the RGB states; left steps back. */
    events |= debounce(&left_button,
                       button_pressed(GPIOB_BASE, BOARD_BUTTON_LEFT_PIN),
                       BOARD_BUTTON_LEFT_DOWN, BOARD_BUTTON_LEFT,
                       BOARD_BUTTON_LEFT_LONG, BOARD_BUTTON_LEFT_UP);
    events |= debounce(&right_button,
                       button_pressed(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN),
                       BOARD_BUTTON_RIGHT_DOWN, BOARD_BUTTON_RIGHT,
                       BOARD_BUTTON_RIGHT_LONG, BOARD_BUTTON_RIGHT_UP);
    return events;
}

void board_rgb_set(uint8_t logical_rgb_mask)
{
    rgb_channels = logical_rgb_mask & 7u;
    rgb_level[0] = (rgb_channels & 1u) ? 255u : 0u;
    rgb_level[1] = (rgb_channels & 2u) ? 255u : 0u;
    rgb_level[2] = (rgb_channels & 4u) ? 255u : 0u;
    board_rgb_apply();
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
    /* A polling firmware loop derives its 10 ms cadence from this hardware tick. */
    ++board_millis;
    if (++board_uptime_millisecond_remainder >= 1000u) {
        board_uptime_millisecond_remainder = 0u;
        ++board_uptime_seconds;
    }
}
