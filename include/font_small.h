#ifndef FONT_SMALL_H
#define FONT_SMALL_H

#include <stdbool.h>
#include <stdint.h>

#define FONT_SMALL_CELL_WIDTH 7u
#define FONT_SMALL_CELL_HEIGHT 10u
#define FONT_SMALL_GLYPH_SUN ((char)0x01u)
#define FONT_SMALL_GLYPH_BATTERY ((char)0x02u)

bool font_small_has_glyph(char glyph);
bool font_small_pixel(char glyph, uint8_t x, uint8_t y);

#endif
