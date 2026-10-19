#include "font_large.h"

#define SEG_TOP    (1u << 0u)
#define SEG_UPPER_RIGHT (1u << 1u)
#define SEG_LOWER_RIGHT (1u << 2u)
#define SEG_BOTTOM (1u << 3u)
#define SEG_LOWER_LEFT (1u << 4u)
#define SEG_UPPER_LEFT (1u << 5u)
#define SEG_MIDDLE (1u << 6u)
#define SEGMENT_THICKNESS 4u

static uint8_t digit_segments(uint8_t digit)
{
    static const uint8_t segments[10] = {
        SEG_TOP | SEG_UPPER_RIGHT | SEG_LOWER_RIGHT | SEG_BOTTOM |
            SEG_LOWER_LEFT | SEG_UPPER_LEFT,
        SEG_UPPER_RIGHT | SEG_LOWER_RIGHT,
        SEG_TOP | SEG_UPPER_RIGHT | SEG_MIDDLE | SEG_LOWER_LEFT | SEG_BOTTOM,
        SEG_TOP | SEG_UPPER_RIGHT | SEG_MIDDLE | SEG_LOWER_RIGHT | SEG_BOTTOM,
        SEG_UPPER_LEFT | SEG_UPPER_RIGHT | SEG_MIDDLE | SEG_LOWER_RIGHT,
        SEG_TOP | SEG_UPPER_LEFT | SEG_MIDDLE | SEG_LOWER_RIGHT | SEG_BOTTOM,
        SEG_TOP | SEG_UPPER_LEFT | SEG_MIDDLE | SEG_LOWER_LEFT |
            SEG_LOWER_RIGHT | SEG_BOTTOM,
        SEG_TOP | SEG_UPPER_RIGHT | SEG_LOWER_RIGHT,
        SEG_TOP | SEG_UPPER_RIGHT | SEG_LOWER_RIGHT | SEG_BOTTOM |
            SEG_LOWER_LEFT | SEG_UPPER_LEFT | SEG_MIDDLE,
        SEG_TOP | SEG_UPPER_RIGHT | SEG_LOWER_RIGHT | SEG_BOTTOM |
            SEG_UPPER_LEFT | SEG_MIDDLE
    };
    return segments[digit];
}

static int8_t glyph_index(char glyph)
{
    if (glyph >= '0' && glyph <= '9') {
        return (int8_t)(glyph - '0');
    }
    if (glyph == ':') {
        return 10;
    }
    if (glyph == '.') {
        return 11;
    }
    return -1;
}

bool font_large_has_glyph(char glyph)
{
    return glyph_index(glyph) >= 0;
}

bool font_large_pixel(char glyph, uint8_t x, uint8_t y)
{
    int8_t index = glyph_index(glyph);
    uint8_t segments;
    bool dot;

    if (index < 0 || x >= FONT_LARGE_CELL_WIDTH ||
        y >= FONT_LARGE_CELL_HEIGHT) {
        return false;
    }

    if (glyph == '.') {
        return x >= 9u && x < 9u + SEGMENT_THICKNESS &&
               y >= 29u && y < 29u + SEGMENT_THICKNESS;
    }
    if (glyph == ':') {
        dot = x >= 9u && x < 9u + SEGMENT_THICKNESS &&
            ((y >= 7u && y < 7u + SEGMENT_THICKNESS) ||
             (y >= 23u && y < 23u + SEGMENT_THICKNESS));
        return dot;
    }

    segments = digit_segments((uint8_t)index);
    if ((segments & SEG_TOP) != 0u && x >= 4u && x < 19u && y < SEGMENT_THICKNESS) {
        return true;
    }
    if ((segments & SEG_MIDDLE) != 0u && x >= 4u && x < 19u &&
        y >= 14u && y < 14u + SEGMENT_THICKNESS) {
        return true;
    }
    if ((segments & SEG_BOTTOM) != 0u && x >= 4u && x < 19u &&
        y >= 29u && y < 29u + SEGMENT_THICKNESS) {
        return true;
    }
    if ((segments & SEG_UPPER_LEFT) != 0u && x < SEGMENT_THICKNESS &&
        y >= 3u && y < 16u) {
        return true;
    }
    if ((segments & SEG_LOWER_LEFT) != 0u && x < SEGMENT_THICKNESS &&
        y >= 16u && y < 30u) {
        return true;
    }
    if ((segments & SEG_UPPER_RIGHT) != 0u && x >= 19u &&
        y >= 3u && y < 16u) {
        return true;
    }
    return (segments & SEG_LOWER_RIGHT) != 0u && x >= 19u &&
           y >= 16u && y < 30u;
}
