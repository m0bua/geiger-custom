#include "button_input.h"

#define DEBOUNCE_MS 20u
#define DEFAULT_LONG_PRESS_MS 900u

void button_input_init(button_input_t *button, bool pressed, uint32_t now)
{
    button->candidate_since = now;
    button->pressed_since = now;
    button->repeated_at = now;
    button->long_press_ms = DEFAULT_LONG_PRESS_MS;
    button->candidate = pressed;
    button->stable = false;
    button->suppressed = pressed;
    button->long_reported = false;
}

void button_input_set_long_press_ms(button_input_t *button, uint16_t threshold_ms)
{
    if (button != 0 && threshold_ms != 0u) {
        button->long_press_ms = threshold_ms;
    }
}

void button_input_suppress(button_input_t *button, bool pressed, uint32_t now)
{
    button_input_init(button, pressed, now);
    /* Also consume a tap whose release arrived before the main loop woke. */
    button->suppressed = true;
}

uint8_t button_input_update(button_input_t *button, bool pressed, uint32_t now)
{
    uint8_t events = 0u;
    const uint32_t long_press_ms = button->long_press_ms != 0u ?
        button->long_press_ms : DEFAULT_LONG_PRESS_MS;

    if (pressed != button->candidate) {
        button->candidate = pressed;
        button->candidate_since = now;
    }

    if (button->suppressed) {
        if (!pressed && (uint32_t)(now - button->candidate_since) >= DEBOUNCE_MS) {
            /* No UP event is required: a wake tap can be shorter than debounce. */
            button->suppressed = false;
            button->stable = false;
            button->long_reported = false;
        }
        return 0u;
    }

    if (button->stable != pressed &&
        (uint32_t)(now - button->candidate_since) >= DEBOUNCE_MS) {
        button->stable = pressed;
        if (pressed) {
            button->pressed_since = button->candidate_since;
            button->long_reported = false;
            events |= BUTTON_INPUT_DOWN;
        } else {
            events |= BUTTON_INPUT_UP;
            if (!button->long_reported) {
                /* Use the release edge, not the end of release debounce. */
                events |= (uint32_t)(button->candidate_since -
                    button->pressed_since) >= long_press_ms ?
                        BUTTON_INPUT_LONG : BUTTON_INPUT_CLICK;
            }
            button->long_reported = false;
        }
    }

    /* A release being debounced must not turn a short click into a long one. */
    if (button->stable && pressed) {
        const uint32_t held_ms = now - button->pressed_since;
        if (!button->long_reported && held_ms >= long_press_ms) {
            button->long_reported = true;
            button->repeated_at = now;
            events |= BUTTON_INPUT_LONG;
        } else if (button->long_reported) {
            const uint32_t interval_ms = held_ms >= long_press_ms + 4000u ? 20u :
                (held_ms >= long_press_ms + 3000u ? 30u :
                (held_ms >= long_press_ms + 2000u ? 50u :
                (held_ms >= long_press_ms + 1000u ? 70u : 100u)));
            if ((uint32_t)(now - button->repeated_at) >= interval_ms) {
                button->repeated_at = now;
                events |= BUTTON_INPUT_LONG;
            }
        }
    }
    return events;
}
