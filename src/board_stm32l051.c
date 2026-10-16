#include "board.h"
#include "board_config.h"

#include <stdbool.h>
#include <stdint.h>

volatile uint32_t board_millis;

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define RCC_BASE       0x40021000u
#define RCC_CR         REG32(RCC_BASE + 0x00u)
#define RCC_CFGR       REG32(RCC_BASE + 0x0Cu)
#define RCC_IOPENR     REG32(RCC_BASE + 0x2Cu)
#define RCC_APB1ENR    REG32(RCC_BASE + 0x38u)
#define RCC_APB2ENR    REG32(RCC_BASE + 0x34u)
#define SPI2_BASE      0x40003800u
#define SPI_CR1        0x00u
#define SPI_CR2        0x04u
#define SPI_SR         0x08u
#define SPI_DR         0x0Cu
#define FLASH_ACR      REG32(0x40022000u)

#define GPIOA_BASE     0x50000000u
#define GPIOB_BASE     0x50000400u
#define GPIO_MODER     0x00u
#define GPIO_OSPEEDR   0x08u
#define GPIO_PUPDR     0x0Cu
#define GPIO_IDR       0x10u
#define GPIO_BSRR      0x18u
#define GPIO_AFRL      0x20u
#define GPIO_AFRH      0x24u

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
#define LONG_PRESS_TICKS 80u
#define LCD_QUEUE_CAPACITY 192u
#define LCD_SPI_ENABLED 1u
#define LCD_DIAGNOSTIC_COMMANDS 1u
#define LCD_WIDTH 160u
#define LCD_HEIGHT 80u
#define LCD_Y_OFFSET 24u
#define LCD_TEST_SQUARE_SIZE 15u

#define SYST_CSR       REG32(0xE000E010u)
#define SYST_RVR       REG32(0xE000E014u)
#define SYST_CVR       REG32(0xE000E018u)

typedef struct {
    uint8_t stable;
    uint8_t candidate;
    uint8_t count;
    uint8_t long_reported;
    uint16_t held_ticks;
} button_debounce_t;

static button_debounce_t left_button;
static button_debounce_t right_button;
static uint8_t rgb_brightness_percent = 100u;
static uint8_t rgb_channels;
static bool lcd_spi_ready = true;
static bool lcd_spi_error;
static bool lcd_diag_reported;
static bool lcd_pin_activity_seen;
static bool lcd_control_error;
static bool lcd_started;
static bool lcd_ready;
static uint8_t lcd_init_state;
static uint32_t lcd_init_deadline;
static bool lcd_fill_start_pending;
static bool lcd_fill_active;
static bool lcd_fill_center_square;
/* With the stock init sequence and SPI2 restored, render the full active
 * area so a remaining geometry/offset issue cannot hide a small test mark. */
static bool lcd_center_square_pending;
static uint8_t lcd_fill_byte_phase;
static bool lcd_color_dirty;
static uint16_t lcd_draw_pixel;
static uint32_t lcd_draw_position;
static uint8_t lcd_requested_color;
typedef struct {
    uint8_t value;
    uint8_t is_data;
} lcd_queue_item_t;
static lcd_queue_item_t lcd_queue[LCD_QUEUE_CAPACITY];
static uint16_t lcd_queue_head;
static uint16_t lcd_queue_tail;

enum {
    LCD_INIT_IDLE = 0u,
    LCD_INIT_RESET_LOW,
    LCD_INIT_RESET_HIGH,
    LCD_INIT_PANEL_UNLOCK_TX,
    LCD_INIT_PANEL_UNLOCK_DELAY,
    LCD_INIT_SLPOUT_TX,
    LCD_INIT_SLPOUT_DELAY,
    LCD_INIT_CONFIG_TX,
    LCD_INIT_NORON_DELAY,
    LCD_INIT_NORON_TX,
    LCD_INIT_DISPON_DELAY,
    LCD_INIT_DISPON_TX,
    LCD_INIT_FINAL_DELAY,
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

    /* Keep the LCD selected for reset and the complete init command stream.
     * The stock firmware asserts PA11 before reset/init and releases it only
     * after the initialization transaction has completed. */
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
    REG32(GPIOA_BASE + GPIO_MODER) = (REG32(GPIOA_BASE + GPIO_MODER) &
        ~(3u << 22u)) | (1u << 22u);
    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 11u);
    lcd_check_output_level(GPIOA_BASE, 11u, false);

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
    /* Master, software NSS high, mode 0. DIV32 gives enough time to sample
     * the actual SCK/MOSI pad levels while a byte is being shifted. */
    REG32(SPI2_BASE + SPI_CR1) = (1u << 2u) | (1u << 8u) |
                                  (1u << 9u) | (4u << 3u) | (1u << 6u);

    REG32(GPIOA_BASE + GPIO_BSRR) = 1u << (16u + 8u);
    lcd_check_output_level(GPIOA_BASE, 8u, false);
    lcd_init_deadline = board_millis + 5u;
    lcd_init_state = LCD_INIT_RESET_LOW;
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
    uint32_t previous_pins = REG32(GPIOB_BASE + GPIO_IDR) &
                             ((1u << 13u) | (1u << 15u));
    while ((REG32(SPI2_BASE + SPI_SR) & 1u) == 0u && --timeout != 0u) {
        uint32_t pins = REG32(GPIOB_BASE + GPIO_IDR) &
                        ((1u << 13u) | (1u << 15u));
        if (pins != previous_pins) {
            lcd_pin_activity_seen = true;
        }
        previous_pins = pins;
    }
    if (timeout == 0u) {
        return false;
    }
    if ((REG32(GPIOB_BASE + GPIO_IDR) & ((1u << 13u) | (1u << 15u))) !=
        previous_pins) {
        lcd_pin_activity_seen = true;
    }
    (void)*(volatile uint8_t *)(uintptr_t)(SPI2_BASE + SPI_DR);

    timeout = 100000u;
    while ((REG32(SPI2_BASE + SPI_SR) & (1u << 7u)) != 0u && --timeout != 0u) { }
    return timeout != 0u;
}

static void lcd_spi_pump(void)
{

    if (!lcd_spi_ready) {
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
            board_rgb_set(1u); /* diagnostic: SPI timeout */
        }
        return;
    }

    if (lcd_fill_start_pending) {
        lcd_fill_start_pending = false;
        lcd_fill_active = true;
        lcd_fill_byte_phase = 0u;
    }

    if (lcd_fill_active) {
        uint16_t pixel = lcd_draw_pixel;
        uint8_t value = lcd_fill_byte_phase == 0u ?
            (uint8_t)(pixel >> 8u) : (uint8_t)pixel;
        REG32(GPIOB_BASE + GPIO_BSRR) = 1u << 11u; /* PB11 D/C data */
        lcd_check_output_level(GPIOB_BASE, 11u, true);
        if (!lcd_spi_send_byte(value)) {
            lcd_spi_error = true;
            lcd_spi_ready = false;
            board_rgb_set(1u); /* diagnostic: SPI timeout */
            return;
        }
        if (lcd_fill_byte_phase == 0u) {
            lcd_fill_byte_phase = 1u;
        } else {
            lcd_fill_byte_phase = 0u;
            ++lcd_draw_position;
            uint32_t pixel_count = lcd_fill_center_square ?
                LCD_TEST_SQUARE_SIZE * LCD_TEST_SQUARE_SIZE : LCD_WIDTH * LCD_HEIGHT;
            if (lcd_draw_position >= pixel_count) {
                lcd_fill_active = false;
                lcd_fill_center_square = false;
                REG32(GPIOA_BASE + GPIO_BSRR) = 1u << 11u; /* CS high */
                lcd_check_output_level(GPIOA_BASE, 11u, true);
                if (!lcd_diag_reported) {
                    lcd_diag_reported = true;
                    uint8_t color = 1u; /* red = SPI timeout */
                    if (!lcd_spi_error) {
                        color = !lcd_pin_activity_seen ? 4u :
                                (lcd_control_error ? 3u : 2u);
                    }
                    board_rgb_set(color);
                    /* blue = no SPI pin activity; yellow = control mismatch;
                     * green = SPI activity and control levels match. */
                }
            }
        }
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
        REG32(GPIOB_BASE + GPIO_BSRR) = 1u << BOARD_LCD_BACKLIGHT_PIN;
    } else {
        REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + BOARD_LCD_BACKLIGHT_PIN);
    }
}

void board_display_set_color(uint8_t logical_rgb_mask)
{
    lcd_requested_color = logical_rgb_mask & 7u;
    lcd_color_dirty = true;
}

static void lcd_begin_fill(void)
{
    bool center_square = lcd_center_square_pending;
    uint8_t x_start = 0u;
    uint8_t x_end = (uint8_t)(LCD_WIDTH - 1u);
    uint8_t y_start = (uint8_t)LCD_Y_OFFSET;
    uint8_t y_end = (uint8_t)(LCD_Y_OFFSET + LCD_HEIGHT - 1u);
    uint16_t red = (lcd_requested_color & 1u) != 0u ? 31u : 0u;
    uint16_t green = (lcd_requested_color & 2u) != 0u ? 63u : 0u;
    uint16_t blue = (lcd_requested_color & 4u) != 0u ? 31u : 0u;
    uint8_t x_window[4];
    uint8_t y_window[4];

    if (center_square) {
        x_start = (uint8_t)((LCD_WIDTH - LCD_TEST_SQUARE_SIZE) / 2u);
        x_end = (uint8_t)(x_start + LCD_TEST_SQUARE_SIZE - 1u);
        y_start = (uint8_t)(LCD_Y_OFFSET +
            (LCD_HEIGHT - LCD_TEST_SQUARE_SIZE) / 2u);
        y_end = (uint8_t)(y_start + LCD_TEST_SQUARE_SIZE - 1u);
    }
    x_window[0] = 0u;
    x_window[1] = x_start;
    x_window[2] = 0u;
    x_window[3] = x_end;
    y_window[0] = 0u;
    y_window[1] = y_start;
    y_window[2] = 0u;
    y_window[3] = y_end;

    lcd_draw_pixel = (uint16_t)((red << 11u) | (green << 5u) | blue);
    lcd_fill_center_square = center_square;
    lcd_center_square_pending = false;
    lcd_draw_position = 0u;
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
    lcd_color_dirty = false;
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
            /* Stock panel requires its vendor unlock before leaving sleep. */
            { const uint8_t unlock = 0x80u; lcd_command(0x0Fu, &unlock, 1u); }
            lcd_init_state = LCD_INIT_PANEL_UNLOCK_TX;
#endif
        }
        return;
    case LCD_INIT_PANEL_UNLOCK_TX:
        if (lcd_queue_drained()) {
            lcd_init_deadline = board_millis + 150u;
            lcd_init_state = LCD_INIT_PANEL_UNLOCK_DELAY;
        }
        return;
    case LCD_INIT_PANEL_UNLOCK_DELAY:
        if (deadline_reached(lcd_init_deadline)) {
            lcd_command(0x11u, 0, 0u); /* SLPOUT */
            lcd_init_state = LCD_INIT_SLPOUT_TX;
        }
        return;
    case LCD_INIT_SLPOUT_TX:
        if (lcd_queue_drained()) {
            lcd_init_deadline = board_millis + 255u;
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
            lcd_ready = true;
            lcd_init_state = LCD_INIT_READY;
            /* Paint a diagnostic frame immediately after controller startup.
             * The backlight remains independently controlled by the keys. */
            lcd_requested_color = 7u;
            lcd_color_dirty = true;
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
    if (lcd_color_dirty && lcd_queue_drained() && !lcd_fill_active) {
        lcd_begin_fill();
    }
}

bool board_display_busy(void)
{
    if (!lcd_started) {
        return false;
    }
    return !lcd_ready || !lcd_queue_drained() || lcd_fill_start_pending ||
           lcd_fill_active || lcd_color_dirty;
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

static bool button_pressed(uint32_t base, uint32_t pin)
{
    uint32_t level = gpio_read(base, pin);
    return BOARD_BUTTON_ACTIVE_LOW ? (level == 0u) : (level != 0u);
}

static uint8_t debounce(button_debounce_t *button, bool raw_pressed,
                        uint8_t press_event, uint8_t short_event,
                        uint8_t long_event)
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
        } else {
            if (button->long_reported == 0u) {
                events |= short_event;
            }
            button->held_ticks = 0u;
            button->long_reported = 0u;
        }
    }

    if (button->stable != 0u && button->long_reported == 0u) {
        if (button->held_ticks < LONG_PRESS_TICKS) {
            ++button->held_ticks;
        }
        if (button->held_ticks >= LONG_PRESS_TICKS) {
            button->long_reported = 1u;
            events |= long_event;
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

    /* Configure one Flash wait state before switching to 16 MHz HSI. */
    FLASH_ACR = (FLASH_ACR & ~1u) | 1u;

    /* Reset-clock startup: switch to HSI16 and wait until it is ready. */
    RCC_CR |= 1u; /* HSI16ON */
    while ((RCC_CR & (1u << 2)) == 0u) { } /* HSI16RDY */
    RCC_CFGR = (RCC_CFGR & ~3u) | 1u;
    while ((RCC_CFGR & (3u << 2)) != (1u << 2)) { }

    RCC_IOPENR |= 3u; /* GPIOA and GPIOB clocks */
    (void)RCC_IOPENR;

    /* Preload the assumed inactive LED level before enabling output mode. */
    REG32(GPIOA_BASE + GPIO_BSRR) = BOARD_RGB_ACTIVE_HIGH ? (led_mask << 16) : led_mask;

    RCC_APB1ENR |= 1u;       /* TIM2 clock */
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

    /* PB2 follows the stock active-mode on / LCD-sleep off sequence.
     * Its association with LCD backlight remains a hardware hypothesis. */
    REG32(GPIOB_BASE + GPIO_BSRR) = 1u << (16u + BOARD_LCD_BACKLIGHT_PIN);
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

    SYST_RVR = 16000u - 1u; /* 1 ms at HSI16 */
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
                       BOARD_BUTTON_LEFT_LONG);
    events |= debounce(&right_button,
                       button_pressed(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN),
                       BOARD_BUTTON_RIGHT_DOWN, BOARD_BUTTON_RIGHT,
                       BOARD_BUTTON_RIGHT_LONG);
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
}
