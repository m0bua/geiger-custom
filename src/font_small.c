#include "font_small.h"

/* First 13 entries are shared with the large font; remaining printable
 * glyphs are kept in predictable ASCII order, followed by icon glyphs. */
#define SMALL_GLYPH_COUNT 51u

static const char small_glyphs[SMALL_GLYPH_COUNT + 1u] =
    "0123456789:. %+-/ABCDEFGHIJKLMNOPQRSTUVWXYZhmnpv\x01\x02\xB5";

static const uint8_t small_rows[SMALL_GLYPH_COUNT][7] = {
    { 14, 17, 19, 21, 25, 17, 14 }, /* 0 */
    { 4, 12, 4, 4, 4, 4, 14 }, /* 1 */
    { 14, 17, 1, 2, 4, 8, 31 }, /* 2 */
    { 30, 1, 1, 14, 1, 1, 30 }, /* 3 */
    { 2, 6, 10, 18, 31, 2, 2 }, /* 4 */
    { 31, 16, 16, 30, 1, 1, 30 }, /* 5 */
    { 14, 16, 16, 30, 17, 17, 14 }, /* 6 */
    { 31, 1, 2, 4, 8, 8, 8 }, /* 7 */
    { 14, 17, 17, 14, 17, 17, 14 }, /* 8 */
    { 14, 17, 17, 15, 1, 1, 14 }, /* 9 */
    { 0, 4, 4, 0, 4, 4, 0 }, /* : */
    { 0, 0, 0, 0, 0, 4, 4 }, /* . */
    { 0, 0, 0, 0, 0, 0, 0 }, /* space */
    { 17, 2, 4, 4, 8, 17, 0 }, /* % */
    { 0, 4, 4, 31, 4, 4, 0 }, /* + */
    { 0, 0, 0, 31, 0, 0, 0 }, /* - */
    { 1, 2, 2, 4, 8, 8, 16 }, /* / */
    { 14, 17, 17, 31, 17, 17, 17 }, /* A */
    { 30, 17, 17, 30, 17, 17, 30 }, /* B */
    { 14, 17, 16, 16, 16, 17, 14 }, /* C */
    { 30, 17, 17, 17, 17, 17, 30 }, /* D */
    { 31, 16, 16, 30, 16, 16, 31 }, /* E */
    { 31, 16, 16, 30, 16, 16, 16 }, /* F */
    { 14, 17, 16, 23, 17, 17, 15 }, /* G */
    { 17, 17, 17, 31, 17, 17, 17 }, /* H */
    { 14, 4, 4, 4, 4, 4, 14 }, /* I */
    { 7, 2, 2, 2, 2, 18, 12 }, /* J */
    { 17, 18, 20, 24, 20, 18, 17 }, /* K */
    { 16, 16, 16, 16, 16, 16, 31 }, /* L */
    { 17, 27, 21, 21, 17, 17, 17 }, /* M */
    { 17, 25, 21, 19, 17, 17, 17 }, /* N */
    { 14, 17, 17, 17, 17, 17, 14 }, /* O */
    { 30, 17, 17, 30, 16, 16, 16 }, /* P */
    { 14, 17, 17, 17, 21, 18, 13 }, /* Q */
    { 30, 17, 17, 30, 20, 18, 17 }, /* R */
    { 15, 16, 16, 14, 1, 1, 30 }, /* S */
    { 31, 4, 4, 4, 4, 4, 4 }, /* T */
    { 17, 17, 17, 17, 17, 17, 14 }, /* U */
    { 17, 17, 17, 17, 17, 10, 4 }, /* V */
    { 17, 17, 17, 21, 21, 21, 10 }, /* W */
    { 17, 17, 10, 4, 10, 17, 17 }, /* X */
    { 17, 17, 10, 4, 4, 4, 4 }, /* Y */
    { 31, 1, 2, 4, 8, 16, 31 }, /* Z */
    { 0, 16, 16, 30, 17, 17, 17 }, /* h */
    { 0, 0, 26, 21, 21, 21, 21 }, /* m */
    { 0, 0, 30, 17, 17, 17, 17 }, /* n */
    { 0, 0, 30, 17, 17, 30, 16 }, /* p */
    { 0, 0, 17, 17, 17, 10, 4 }, /* v */
    { 4, 21, 14, 31, 14, 21, 4 }, /* sun icon */
    { 4, 31, 17, 17, 17, 17, 31 }, /* battery icon */
    { 0, 0, 17, 17, 17, 19, 13 } /* micro sign; descender below */
};

static const uint8_t micro_rows[8] = {
    0, 0, 17, 17, 17, 19, 29, 16
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
    int8_t index = small_glyph_index(glyph);

    if (index < 0 || x >= FONT_SMALL_CELL_WIDTH ||
        y >= FONT_SMALL_CELL_HEIGHT || x == 0u || x >= 6u || y == 0u) {
        return false;
    }
    if (glyph == (char)0xB5u) {
        return (micro_rows[y - 1u] & (uint8_t)(1u << (5u - x))) != 0u;
    }
    /* The micro sign alone uses the eighth bitmap row as a descender. */
    if (y >= 8u) {
        return false;
    }
    return (small_rows[(uint8_t)index][y - 1u] &
            (uint8_t)(1u << (5u - x))) != 0u;
}
