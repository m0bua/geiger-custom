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
#define FLASH_ACR      REG32(0x40022000u)

#define GPIOA_BASE     0x50000000u
#define GPIOB_BASE     0x50000400u
#define GPIO_MODER     0x00u
#define GPIO_PUPDR     0x0Cu
#define GPIO_IDR       0x10u
#define GPIO_BSRR      0x18u
#define GPIO_AFRL      0x20u

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
                        uint8_t short_event, uint8_t long_event)
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
                       BOARD_BUTTON_LEFT, BOARD_BUTTON_LEFT_LONG);
    events |= debounce(&right_button,
                       button_pressed(GPIOA_BASE, BOARD_BUTTON_RIGHT_PIN),
                       BOARD_BUTTON_RIGHT, BOARD_BUTTON_RIGHT_LONG);
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
