#include "font_small.h"

/* First 13 entries are shared with the large font; remaining printable
 * glyphs are kept in predictable ASCII order, followed by icon glyphs. */
#define SMALL_GLYPH_COUNT 53u

static const char small_glyphs[SMALL_GLYPH_COUNT + 1u] =
    "0123456789:. %-/<>?ABCDEFGHIKLMNOPRSTUVWXYZdhmnpsv\x01\x02\xB5";

/* One byte per bitmap column; bit 0 is the top row. The micro sign
 * uses bit 7 for its descender, so it needs no separate bitmap. */
static const uint8_t small_columns[SMALL_GLYPH_COUNT][5] = {
    { 62, 81, 73, 69, 62 }, /* 0 */
    { 0, 66, 127, 64, 0 }, /* 1 */
    { 66, 97, 81, 73, 70 }, /* 2 */
    { 65, 73, 73, 73, 54 }, /* 3 */
    { 24, 20, 18, 127, 16 }, /* 4 */
    { 79, 73, 73, 73, 49 }, /* 5 */
    { 62, 73, 73, 73, 48 }, /* 6 */
    { 1, 113, 9, 5, 3 }, /* 7 */
    { 54, 73, 73, 73, 54 }, /* 8 */
    { 6, 73, 73, 73, 62 }, /* 9 */
    { 0, 0, 54, 0, 0 }, /* : */
    { 0, 0, 96, 0, 0 }, /* . */
    { 0, 0, 0, 0, 0 }, /* space */
    { 33, 16, 12, 2, 33 }, /* % */
    { 8, 8, 8, 8, 8 }, /* - */
    { 64, 48, 8, 6, 1 }, /* / */
    { 8, 20, 34, 65, 0 }, /* < */
    { 0, 65, 34, 20, 8 }, /* > */
    { 2, 1, 81, 9, 6 }, /* ? */
    { 126, 9, 9, 9, 126 }, /* A */
    { 127, 73, 73, 73, 54 }, /* B */
    { 62, 65, 65, 65, 34 }, /* C */
    { 127, 65, 65, 65, 62 }, /* D */
    { 127, 73, 73, 73, 65 }, /* E */
    { 127, 9, 9, 9, 1 }, /* F */
    { 62, 65, 73, 73, 122 }, /* G */
    { 127, 8, 8, 8, 127 }, /* H */
    { 0, 65, 127, 65, 0 }, /* I */
    { 127, 8, 20, 34, 65 }, /* K */
    { 127, 64, 64, 64, 64 }, /* L */
    { 127, 2, 12, 2, 127 }, /* M */
    { 127, 2, 4, 8, 127 }, /* N */
    { 62, 65, 65, 65, 62 }, /* O */
    { 127, 9, 9, 9, 6 }, /* P */
    { 127, 9, 25, 41, 70 }, /* R */
    { 70, 73, 73, 73, 49 }, /* S */
    { 1, 1, 127, 1, 1 }, /* T */
    { 63, 64, 64, 64, 63 }, /* U */
    { 31, 32, 64, 32, 31 }, /* V */
    { 63, 64, 56, 64, 63 }, /* W */
    { 99, 20, 8, 20, 99 }, /* X */
    { 3, 4, 120, 4, 3 }, /* Y */
    { 97, 81, 73, 69, 67 }, /* Z */
    { 48, 72, 72, 72, 126 }, /* d */
    { 126, 8, 8, 8, 112 }, /* h */
    { 124, 4, 120, 4, 120 }, /* m */
    { 124, 4, 4, 4, 120 }, /* n */
    { 124, 36, 36, 36, 24 }, /* p */
    { 36, 42, 42, 42, 16 }, /* s */
    { 28, 32, 64, 32, 28 }, /* v */
    { 42, 28, 127, 28, 42 }, /* sun icon */
    { 126, 66, 67, 66, 126 }, /* battery icon */
    { 252, 64, 64, 32, 124 }, /* micro sign; descender below */
};

static int8_t small_glyph_index(char glyph)
{
    for (uint8_t i = 0u; i < SMALL_GLYPH_COUNT; ++i) {
        if (small_glyphs[i] == glyph) {
            return (int8_t)i;
        }
    }
    return -1;
}

bool font_small_has_glyph(char glyph)
{
    return small_glyph_index(glyph) >= 0;
}

bool font_small_pixel(char glyph, uint8_t x, uint8_t y)
{
    if (x == 0u || x >= 6u || y == 0u || y >= 9u) {
        return false;
    }
    const int8_t index = small_glyph_index(glyph);
    return index >= 0 &&
        (small_columns[(uint8_t)index][x - 1u] &
            (uint8_t)(1u << (y - 1u))) != 0u;
}
