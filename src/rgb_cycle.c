#include "rgb_cycle.h"

static const uint8_t color_channels[RGB_COLOR_COUNT] = {
    0u, /* off */
    1u, /* red */
    3u, /* yellow: red + green */
    2u, /* green */
    6u, /* cyan: green + blue */
    4u, /* blue */
    5u, /* purple after blue: add red */
    7u  /* white: all channels */
};

void rgb_cycle_init(rgb_cycle_t *cycle)
{
    if (cycle != 0) {
        cycle->color = RGB_COLOR_OFF;
    }
}

rgb_color_t rgb_cycle_step(rgb_cycle_t *cycle, rgb_direction_t direction)
{
    int next;

    if (cycle == 0) {
        return RGB_COLOR_OFF;
    }

    next = (int)cycle->color;
    if (direction == RGB_STEP_FORWARD) {
        if (next < (int)RGB_COLOR_WHITE) {
            ++next;
        }
    } else if (direction == RGB_STEP_BACKWARD) {
        if (next > (int)RGB_COLOR_OFF) {
            --next;
        }
    }
    cycle->color = (rgb_color_t)next;
    return cycle->color;
}

uint8_t rgb_color_channels(rgb_color_t color)
{
    /* Unsigned conversion rejects both out-of-range positive values and
     * negative values (which wrap above RGB_COLOR_COUNT). */
    if ((uint32_t)color >= (uint32_t)RGB_COLOR_COUNT) {
        return 0u;
    }
    return color_channels[color];
}
