#ifndef RGB_CYCLE_H
#define RGB_CYCLE_H

#include <stdint.h>

typedef enum {
    RGB_COLOR_OFF = 0,
    RGB_COLOR_RED,
    RGB_COLOR_YELLOW,
    RGB_COLOR_GREEN,
    RGB_COLOR_CYAN,
    RGB_COLOR_BLUE,
    RGB_COLOR_PURPLE_AFTER_BLUE,
    RGB_COLOR_WHITE,
    RGB_COLOR_COUNT
} rgb_color_t;

typedef enum {
    RGB_STEP_BACKWARD = -1,
    RGB_STEP_FORWARD = 1
} rgb_direction_t;

typedef struct {
    rgb_color_t color;
} rgb_cycle_t;

/* Channel bits use logical order R=bit0, G=bit1, B=bit2. The board layer
 * must map these logical levels to physical pins and active polarity. */
void rgb_cycle_init(rgb_cycle_t *cycle);
rgb_color_t rgb_cycle_step(rgb_cycle_t *cycle, rgb_direction_t direction);
uint8_t rgb_color_channels(rgb_color_t color);

#endif
