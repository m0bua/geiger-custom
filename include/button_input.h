#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include <stdbool.h>
#include <stdint.h>

enum {
    BUTTON_INPUT_DOWN = 1u,
    BUTTON_INPUT_UP = 2u,
    BUTTON_INPUT_CLICK = 4u,
    BUTTON_INPUT_LONG = 8u
};

typedef struct {
    uint32_t candidate_since;
    uint32_t pressed_since;
    uint32_t repeated_at;
    uint16_t long_press_ms;
    bool candidate;
    bool stable;
    bool suppressed;
    bool long_reported;
} button_input_t;

void button_input_init(button_input_t *button, bool pressed, uint32_t now);
void button_input_suppress(button_input_t *button, bool pressed, uint32_t now);
void button_input_set_long_press_ms(button_input_t *button, uint16_t threshold_ms);
uint8_t button_input_update(button_input_t *button, bool pressed, uint32_t now);

#endif
