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
static volatile uint16_t lcd_adc_battery_raw;
static volatile uint16_t lcd_adc_sun_raw;
static volatile uint16_t lcd_adc_usb_raw;
static bool adc_ready;

#define DETECTOR_GPIO_BASE GPIOB_BASE
#define DETECTOR_PIN       BOARD_DETECTOR_PIN
#define DETECTOR_EXTI_MASK (1u << DETECTOR_PIN)

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define RCC_BASE       0x40021000u
#define RCC_CR         REG32(RCC_BASE + 0x00u)
#define RCC_CFGR       REG32(RCC_BASE + 0x0Cu)
#define RCC_AHBENR     REG32(RCC_BASE + 0x30u)
#define RCC_CCIPR      REG32(RCC_BASE + 0x4Cu)
#define RCC_IOPENR     REG32(RCC_BASE + 0x2Cu)
#define RCC_APB1ENR    REG32(RCC_BASE + 0x38u)
#define RCC_APB2ENR    REG32(RCC_BASE + 0x34u)
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

#define TIM2_BASE      0x40000000u
#define TIM22_BASE     0x40011400u
#define TIM_CR1        0x00u
#define TIM_EGR        0x14u
#define TIM_CCMR1      0x18u
#define TIM_CCER       0x20u
#define TIM_PSC        0x28u
#define TIM_ARR        0x2Cu
#define TIM_CCR1       0x34u
#define TIM_CCR2       0x38u

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
#define LCD_RATE_PREFIX_WIDTH FONT_LARGE_SPACE_WIDTH
#define LCD_CPM_MAX_DIGITS 4u
#define LCD_RATE_FIELD_WIDTH (LCD_RATE_MAX_GLYPHS * FONT_LARGE_CELL_WIDTH + \
    LCD_RATE_PREFIX_WIDTH)
#define LCD_RATE_FIELD_X ((LCD_WIDTH - LCD_RATE_FIELD_WIDTH) / 2u)
#define LCD_RATE_LABEL_LENGTH 5u
#define LCD_DOSE_LABEL_LENGTH 5u
#define LCD_CPM_TEXT_LENGTH (LCD_CPM_MAX_DIGITS + 4u)
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
#define LCD_LARGE_GLYPH_PIXELS (FONT_LARGE_CELL_WIDTH * FONT_LARGE_CELL_HEIGHT)
#define LCD_TOP_ROW_PIXELS (LCD_WIDTH * FONT_SMALL_CELL_HEIGHT)
#define LCD_TEXT_GLYPH_PIXELS ((LCD_LARGE_GLYPH_PIXELS > \
    LCD_TOP_ROW_PIXELS) ? LCD_LARGE_GLYPH_PIXELS : \
    LCD_TOP_ROW_PIXELS)
#define LCD_TEXT_FOREGROUND 0xD6BAu
#define LCD_TEXT_BACKGROUND 0x0000u

static void adc_wait_ms(uint32_t duration);

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
static bool lcd_text_row_commands_queued;
static uint8_t lcd_text_glyph_index;
static uint8_t lcd_text_dma_glyph_advance;
static uint16_t lcd_text_glyph_pixels;
static char lcd_rate_glyphs[LCD_RATE_MAX_GLYPHS + 1u] = ".0000";
static uint8_t lcd_rate_glyph_count = LCD_RATE_MAX_GLYPHS;
static uint16_t lcd_rate_color = LCD_COUNTER_CYAN;
static uint8_t lcd_rate_led_mask = 6u;
static char lcd_rate_label[] = "mSv/h";
static char lcd_display_unit_prefix = 'm';
static char lcd_cpm_text[LCD_CPM_TEXT_LENGTH + 1u] = "   0 CPM";
static char lcd_adc_glyphs[LCD_ADC_TEXT_LENGTH + 1u] =
    "      \x01  0% \x02  0% ";
static uint16_t lcd_displayed_battery_percent;
static uint16_t lcd_displayed_solar_percent;
static uint8_t lcd_screen_page;
static uint8_t lcd_main_return_page;
static uint32_t lcd_time_displayed_minute = UINT32_MAX;
static uint32_t lcd_rate_value_thousandths;
static uint32_t lcd_dose_value_thousandths;
static uint32_t lcd_dose_fraction;
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

static void lcd_format_scaled_sv(uint64_t value_picosv_x18)
{
    /* Scales are multiplied by 18 to retain the dose accumulator's
     * 1/18-pSv remainder without floating-point arithmetic. */
    static const uint64_t unit_scales[] = {
        18000000000ull, 18000000u, 18000u, 18u
    };
    static const char unit_prefixes[] = { 'm', (char)0xB5u, 'n', 'p' };
    uint8_t unit = 3u;
    uint8_t fallback_unit = 3u;
    bool has_fallback = false;

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
    if (has_fallback) {
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
        uint8_t integer_places;
        uint8_t decimals;
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
        integer_places = integer_digits < 2u ? 2u : integer_digits;
        decimals = integer_places < 5u ?
            (uint8_t)(5u - integer_places) : 0u;
        for (uint8_t i = 0u; i < decimals; ++i) {
            factor *= 10u;
        }
        scaled = (value_picosv_x18 * factor + scale / 2u) / scale;
        integer_part = scaled / factor;

        /* Rounding can cross 1000. Move to the next coarser unit so the
         * displayed value keeps at most three places before the separator. */
        if (integer_part >= 1000u && unit != 0u) {
            --unit;
            continue;
        }

        fraction = scaled % factor;
        value = integer_part;
        integer_digits = 0u;
        do {
            reversed[integer_digits++] = (char)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u && integer_digits < sizeof(reversed));

        lcd_rate_glyph_count = 0u;
        if (integer_digits < 2u) {
            lcd_rate_glyphs[lcd_rate_glyph_count++] = '0';
        }
        while (integer_digits != 0u) {
            lcd_rate_glyphs[lcd_rate_glyph_count++] =
                reversed[--integer_digits];
        }
        lcd_rate_glyphs[lcd_rate_glyph_count++] = '.';
        divisor = decimals == 0u ? 1u : factor / 10u;
        for (uint8_t i = 0u; i < decimals; ++i) {
            lcd_rate_glyphs[lcd_rate_glyph_count++] =
                (char)('0' + ((fraction / divisor) % 10u));
            if (divisor > 1u) {
                divisor /= 10u;
            }
        }
        lcd_rate_glyphs[lcd_rate_glyph_count] = '\0';
        lcd_display_unit_prefix = unit_prefixes[unit];
        lcd_rate_label[0] = lcd_display_unit_prefix;
        return;
    }
}

static void lcd_format_rate(uint32_t rate_thousandths)
{
    if (rate_thousandths > LCD_RATE_MAX_VALUE) {
        rate_thousandths = LCD_RATE_MAX_VALUE;
    }
    /* Input is in 0.001 µSv/h (nSv/h), in units of 1/18 pSv. */
    lcd_format_scaled_sv((uint64_t)rate_thousandths * 18000u);
}

static void lcd_format_cpm(uint32_t cpm)
{
    char reversed[LCD_CPM_MAX_DIGITS];
    uint8_t digits = 0u;

    if (cpm > 9999u) {
        cpm = 9999u;
    }
    do {
        reversed[digits++] = (char)('0' + cpm % 10u);
        cpm /= 10u;
    } while (cpm != 0u && digits < sizeof(reversed));

    for (uint8_t i = 0u; i < LCD_CPM_MAX_DIGITS; ++i) {
        lcd_cpm_text[i] = ' ';
    }
    for (uint8_t i = 0u; i < digits; ++i) {
        lcd_cpm_text[LCD_CPM_MAX_DIGITS - digits + i] =
            reversed[digits - i - 1u];
    }
    lcd_cpm_text[LCD_CPM_MAX_DIGITS] = ' ';
    lcd_cpm_text[LCD_CPM_MAX_DIGITS + 1u] = 'C';
    lcd_cpm_text[LCD_CPM_MAX_DIGITS + 2u] = 'P';
    lcd_cpm_text[LCD_CPM_MAX_DIGITS + 3u] = 'M';
    lcd_cpm_text[LCD_CPM_TEXT_LENGTH] = '\0';
}

static void lcd_format_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction)
{
    /* The whole part is in nSv; the remainder is in 1/3600 nSv.
     * One accumulator step is 5/18 pSv; keep that fraction exactly. */
    const uint64_t value_picosv_x18 =
        (uint64_t)dose_thousandths * 18000u +
        (uint64_t)dose_fraction * 5u;
    lcd_format_scaled_sv(value_picosv_x18);
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
                                  uint16_t row_width)
{
    for (uint8_t y = 0u; y < FONT_SMALL_CELL_HEIGHT; ++y) {
        for (uint8_t x = 0u; x < FONT_SMALL_CELL_WIDTH; ++x) {
            const uint16_t pixel_x = (uint16_t)(x_start + x);
            if (pixel_x < row_width && font_small_pixel(glyph, x, y)) {
                lcd_text_glyph[(uint16_t)y * row_width + pixel_x] =
                    LCD_TEXT_FOREGROUND;
            }
        }
    }
}

static void lcd_top_row_draw_percent(uint8_t offset, char icon,
                                    uint8_t right_edge)
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

    lcd_top_row_draw_glyph(icon, x, LCD_WIDTH);
    x = (uint16_t)(x + FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP);
    for (uint8_t i = first; i < offset + 4u; ++i) {
        lcd_top_row_draw_glyph(lcd_adc_glyphs[i], x, LCD_WIDTH);
        x = (uint16_t)(x + FONT_SMALL_CELL_WIDTH + LCD_SMALL_TEXT_GAP);
    }
}

static void lcd_top_row_draw_uptime(void)
{
    const uint32_t total_minutes = board_uptime_seconds / 60u;
    const char time_text[5] = {
        (char)('0' + ((total_minutes / 60u) % 24u) / 10u),
        (char)('0' + ((total_minutes / 60u) % 24u) % 10u),
        ':',
        (char)('0' + (total_minutes % 60u) / 10u),
        (char)('0' + (total_minutes % 60u) % 10u)
    };

    for (uint8_t i = 0u; i < sizeof(time_text); ++i) {
        lcd_top_row_draw_glyph(time_text[i],
            (uint16_t)(4u + i * (FONT_SMALL_CELL_WIDTH +
                                 LCD_SMALL_TEXT_GAP)),
            LCD_WIDTH);
    }
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
        line[0] = '-'; line[1] = '-';
        line[18] = '-'; line[19] = '-';
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

static bool lcd_text_prepare_glyph(void)
{
    if (lcd_screen_page == 2u) {
        const uint8_t step = lcd_text_glyph_index;
        uint8_t x_window[4];
        uint8_t y_window[4];

        if (step >= 7u) {
            lcd_text_active = false;
            return false;
        }

        if (step == 0u || step == 6u) {
            char days_text[16];
            uint8_t days_length = 0u;
            uint32_t days = board_uptime_seconds / 86400u;

            if (step == 0u) {
                static const char header[] = "UPTIME";
                days_length = 6u;
                for (uint8_t i = 0u; i < days_length; ++i) {
                    days_text[i] = header[i];
                }
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
            const uint32_t hour = (total_minutes / 60u) % 24u;
            const uint32_t minute = total_minutes % 60u;
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
        const uint8_t last_glyph = lcd_screen_page == 0u ? cpm_glyph :
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
            lcd_top_row_draw_percent(7u, FONT_SMALL_GLYPH_SUN, 106u);
            lcd_top_row_draw_percent(13u, FONT_SMALL_GLYPH_BATTERY, 154u);

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
                dose_label[0] = lcd_display_unit_prefix;
                dose_label[1] = 'S';
                dose_label[2] = 'v';
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

        if (lcd_text_glyph_index == cpm_glyph && lcd_screen_page == 0u) {
            const uint16_t text_width = LCD_CPM_TEXT_WIDTH;
            const uint8_t y_start =
            (uint8_t)(LCD_HEIGHT - FONT_SMALL_CELL_HEIGHT - 4u);
            uint8_t x_window[4] = {
                0u, 0u, 0u, (uint8_t)(text_width - 1u)
            };
            uint8_t y_window[4] = {
                0u, (uint8_t)(LCD_Y_OFFSET + y_start), 0u,
                (uint8_t)(LCD_Y_OFFSET + y_start +
                          FONT_SMALL_CELL_HEIGHT - 1u)
            };

            lcd_text_glyph_pixels = (uint16_t)(text_width *
                FONT_SMALL_CELL_HEIGHT);
            lcd_text_dma_glyph_advance = 1u;
            for (uint16_t i = 0u; i < lcd_text_glyph_pixels; ++i) {
                lcd_text_glyph[i] = LCD_TEXT_BACKGROUND;
            }
            for (uint8_t column = 0u; column < LCD_CPM_TEXT_LENGTH;
                 ++column) {
                lcd_top_row_draw_glyph(lcd_cpm_text[column],
                    (uint16_t)(column * (FONT_SMALL_CELL_WIDTH +
                                         LCD_SMALL_TEXT_GAP)), text_width);
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
            const bool is_prefix_space = digit_slot == 0u;
            const uint8_t digit_index = is_prefix_space ? 0u :
                (uint8_t)(digit_slot - 1u);
            const uint8_t first_digit = (uint8_t)(LCD_RATE_MAX_GLYPHS -
                lcd_rate_glyph_count);
            uint16_t text_width = 0u;
            uint16_t glyph_x = (uint16_t)(LCD_RATE_FIELD_X +
                LCD_RATE_FIELD_WIDTH);
            uint8_t glyph_width;

            glyph = is_prefix_space ? ' ' :
                (digit_index < first_digit ? ' ' :
                 lcd_rate_glyphs[digit_index - first_digit]);
            for (uint8_t i = 0u; i < lcd_rate_glyph_count; ++i) {
                text_width = (uint16_t)(text_width +
                    font_large_glyph_width(lcd_rate_glyphs[i]));
            }
            glyph_x = (uint16_t)(glyph_x - text_width);
            if (is_prefix_space) {
                glyph_x = LCD_RATE_FIELD_X;
            } else if (digit_index < first_digit) {
                glyph_x = (uint16_t)(LCD_RATE_FIELD_X +
                    LCD_RATE_PREFIX_WIDTH +
                    digit_index * FONT_LARGE_CELL_WIDTH);
            } else {
                for (uint8_t i = 0u; i < digit_index - first_digit; ++i) {
                    glyph_x = (uint16_t)(glyph_x +
                        font_large_glyph_width(lcd_rate_glyphs[i]));
                }
            }
            x_start = (uint8_t)glyph_x;
            glyph_width = digit_index < first_digit ? FONT_LARGE_CELL_WIDTH :
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

void board_display_next_page(void)
{
    if (lcd_screen_page >= 2u) {
        lcd_screen_page = (uint8_t)(2u +
            ((lcd_screen_page - 2u + LCD_EXTRA_SCREEN_COUNT - 1u) %
             LCD_EXTRA_SCREEN_COUNT));
    } else {
        return;
    }
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
    } else {
        lcd_format_dose(lcd_dose_value_thousandths, lcd_dose_fraction);
    }
    lcd_cpm_redraw_pending = true;
}

void board_display_extra_enter(void)
{
    if (lcd_screen_page >= 2u) {
        return;
    }
    lcd_main_return_page = lcd_screen_page;
    lcd_screen_page = 2u;
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
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
    if (lcd_screen_page == 0u) {
        lcd_format_rate(lcd_rate_value_thousandths);
    } else {
        lcd_format_dose(lcd_dose_value_thousandths, lcd_dose_fraction);
    }
    lcd_text_active = false;
    lcd_text_row_commands_queued = false;
    lcd_background_pending = true;
    lcd_cpm_redraw_pending = true;
}

void board_display_set_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction)
{
    static uint32_t previous_dose_thousandths = UINT32_MAX;
    static uint32_t previous_dose_fraction = UINT32_MAX;

    lcd_dose_value_thousandths = dose_thousandths;
    lcd_dose_fraction = dose_fraction;
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

void board_display_set_rate(uint32_t rate_thousandths, uint32_t cpm,
                            bool window_ready)
{
    static uint32_t previous_rate_thousandths = UINT32_MAX;
    static uint32_t previous_cpm = UINT32_MAX;
    bool changed = false;
    const uint64_t rate_scaled = (uint64_t)cpm * 666u;
    uint16_t rate_color = LCD_COUNTER_CYAN;

    lcd_rate_value_thousandths = rate_thousandths;

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
        } else {
            lcd_rate_led_mask = 6u;
        }
        changed = true;
    }

    /* The stock UI retains CPM at zero and rate below 0.01 uSv/h. */
    if (cpm != 0u && cpm != previous_cpm) {
        previous_cpm = cpm;
        lcd_format_cpm(cpm);
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
        if (battery_percent != lcd_displayed_battery_percent) {
            lcd_displayed_battery_percent = battery_percent;
            lcd_format_percent(battery_percent, 13u);
            changed = true;
        }
    }
    if (channel9 <= 4095u) {
        solar_percent = (uint16_t)(channel9 / 31u);
        if (solar_percent != lcd_displayed_solar_percent) {
            lcd_displayed_solar_percent = solar_percent;
            lcd_format_percent(solar_percent, 7u);
            changed = true;
        }
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
    if (lcd_screen_page == 2u && lcd_time_redraw_pending) {
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
        const uint32_t uptime_minute = board_uptime_seconds / 60u;
        if (uptime_minute != lcd_time_displayed_minute) {
            lcd_time_displayed_minute = uptime_minute;
            lcd_time_redraw_pending = true;
        }
    } else if (lcd_screen_page < 2u) {
        const uint32_t uptime_minute = board_uptime_seconds / 60u;
        if (uptime_minute != lcd_time_displayed_minute) {
            lcd_time_displayed_minute = uptime_minute;
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
    lcd_started = true;
    lcd_configure();
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
    if ((EXTI_PR & DETECTOR_EXTI_MASK) != 0u) {
        EXTI_PR = DETECTOR_EXTI_MASK;
        if (detector_pulse_count < 999999u) {
            ++detector_pulse_count;
        }
    }
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
    uint32_t compare;
    if (percent > 100u) {
        percent = 100u;
    }
    rgb_brightness_percent = percent;
    compare = ((uint32_t)percent * PWM_PERIOD) / 100u;
    REG32(TIM2_BASE + TIM_CCR1) = (rgb_channels & 1u) != 0u ? compare : 0u;
    REG32(TIM2_BASE + TIM_CCR2) = (rgb_channels & 2u) != 0u ? compare : 0u;
    REG32(TIM22_BASE + TIM_CCR1) = (rgb_channels & 4u) != 0u ? compare : 0u;
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

    /* Stock enables the detector/power stage by driving PA5 high while active.
     * Keep the evidence-based stock state so the tube can produce pulses. */
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 5u;
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
    uint32_t compare = ((uint32_t)rgb_brightness_percent * PWM_PERIOD) / 100u;
    rgb_channels = logical_rgb_mask & 7u;
    REG32(TIM2_BASE + TIM_CCR1) = (rgb_channels & 1u) != 0u ? compare : 0u;
    REG32(TIM2_BASE + TIM_CCR2) = (rgb_channels & 2u) != 0u ? compare : 0u;
    REG32(TIM22_BASE + TIM_CCR1) = (rgb_channels & 4u) != 0u ? compare : 0u;
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
