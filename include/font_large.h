#ifndef FONT_LARGE_H
#define FONT_LARGE_H

#include <stdbool.h>
#include <stdint.h>

#define FONT_LARGE_CELL_WIDTH 24u
#define FONT_LARGE_CELL_HEIGHT 33u
#define FONT_LARGE_SPACE_WIDTH 13u

bool font_large_has_glyph(char glyph);
uint8_t font_large_glyph_width(char glyph);
bool font_large_pixel(char glyph, uint8_t x, uint8_t y);

#endif
