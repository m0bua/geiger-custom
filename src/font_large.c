#include "font_large.h"

/* Bitmap glyphs rasterized from Anonymous Pro Bold (OFL-1.1).
 * Three bytes per 24-pixel row; digits retain the 24x33 cell geometry. */
#define ROW24(v) { (uint8_t)(v), (uint8_t)((v) >> 8u), (uint8_t)((v) >> 16u) }

#define DOT_LEFT 9u
#define DOT_TOP 26u
#define COLON_LEFT 9u
#define COLON_TOP 10u
#define DASH_LEFT 1u
#define DASH_TOP 19u

static const uint8_t digit_rows[10][FONT_LARGE_CELL_HEIGHT - 3u][3] = {
    { /* 0 */
        ROW24(0x007E00u),
        ROW24(0x03FFC0u),
        ROW24(0x07FFE0u),
        ROW24(0x0FFFF0u),
        ROW24(0x1F81F8u),
        ROW24(0x1F00FCu),
        ROW24(0x3E007Cu),
        ROW24(0x3E007Cu),
        ROW24(0x7F003Eu),
        ROW24(0x7F803Eu),
        ROW24(0x7FC01Eu),
        ROW24(0x7FE01Eu),
        ROW24(0x79F01Fu),
        ROW24(0x78F81Fu),
        ROW24(0x787C1Fu),
        ROW24(0x783E1Fu),
        ROW24(0x781F1Fu),
        ROW24(0x780F9Fu),
        ROW24(0x7807DEu),
        ROW24(0x7C03FEu),
        ROW24(0x7C01FEu),
        ROW24(0x7C00FEu),
        ROW24(0x3E007Cu),
        ROW24(0x3E007Cu),
        ROW24(0x1F00FCu),
        ROW24(0x1F81F8u),
        ROW24(0x0FFFF0u),
        ROW24(0x07FFE0u),
        ROW24(0x03FFC0u),
        ROW24(0x007E00u)
    },
    { /* 1 */
        ROW24(0x00F000u),
        ROW24(0x00F800u),
        ROW24(0x00F800u),
        ROW24(0x00F800u),
        ROW24(0x00FC00u),
        ROW24(0x00FE00u),
        ROW24(0x00FF00u),
        ROW24(0x00FFC0u),
        ROW24(0x00FFF8u),
        ROW24(0x00F7F8u),
        ROW24(0x00F1F8u),
        ROW24(0x00F078u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x00F000u),
        ROW24(0x1FFF80u),
        ROW24(0x1FFF80u),
        ROW24(0x1FFF80u),
        ROW24(0x1FFF80u)
    },
    { /* 2 */
        ROW24(0x00FE00u),
        ROW24(0x03FFC0u),
        ROW24(0x0FFFE0u),
        ROW24(0x0FFFF0u),
        ROW24(0x1F81F8u),
        ROW24(0x3F00F8u),
        ROW24(0x3E007Cu),
        ROW24(0x3E003Cu),
        ROW24(0x3E003Cu),
        ROW24(0x3E003Cu),
        ROW24(0x3E003Cu),
        ROW24(0x3F0000u),
        ROW24(0x1F0000u),
        ROW24(0x1F8000u),
        ROW24(0x0FC000u),
        ROW24(0x07E000u),
        ROW24(0x03F000u),
        ROW24(0x01F800u),
        ROW24(0x00FC00u),
        ROW24(0x007E00u),
        ROW24(0x003F80u),
        ROW24(0x3E1FC0u),
        ROW24(0x3E0FE0u),
        ROW24(0x3E07F0u),
        ROW24(0x3E03F8u),
        ROW24(0x3E01FCu),
        ROW24(0x3FFFFCu),
        ROW24(0x3FFFFCu),
        ROW24(0x3FFFFCu),
        ROW24(0x3FFFFCu)
    },
    { /* 3 */
        ROW24(0x00FE00u),
        ROW24(0x03FF80u),
        ROW24(0x0FFFE0u),
        ROW24(0x0FFFF0u),
        ROW24(0x1F83F0u),
        ROW24(0x3F00F8u),
        ROW24(0x3E0078u),
        ROW24(0x3E007Cu),
        ROW24(0x3E007Cu),
        ROW24(0x3E007Cu),
        ROW24(0x1F0000u),
        ROW24(0x1FC000u),
        ROW24(0x0FFC00u),
        ROW24(0x03FC00u),
        ROW24(0x03FC00u),
        ROW24(0x0FFC00u),
        ROW24(0x1F8000u),
        ROW24(0x3F0000u),
        ROW24(0x3E0000u),
        ROW24(0x3C0000u),
        ROW24(0x3C003Cu),
        ROW24(0x3C003Cu),
        ROW24(0x3C007Cu),
        ROW24(0x3E007Cu),
        ROW24(0x3E00F8u),
        ROW24(0x1F83F8u),
        ROW24(0x1FFFF0u),
        ROW24(0x0FFFE0u),
        ROW24(0x03FFC0u),
        ROW24(0x00FE00u)
    },
    { /* 4 */
        ROW24(0x03F000u),
        ROW24(0x03F800u),
        ROW24(0x03F800u),
        ROW24(0x03FC00u),
        ROW24(0x03FE00u),
        ROW24(0x03FE00u),
        ROW24(0x03EF00u),
        ROW24(0x03EF00u),
        ROW24(0x03E780u),
        ROW24(0x03E7C0u),
        ROW24(0x03E3C0u),
        ROW24(0x03E1E0u),
        ROW24(0x03E1F0u),
        ROW24(0x03E0F0u),
        ROW24(0x03E078u),
        ROW24(0x03E078u),
        ROW24(0x03E03Cu),
        ROW24(0x03E03Eu),
        ROW24(0x7FFFFEu),
        ROW24(0x7FFFFEu),
        ROW24(0x7FFFFEu),
        ROW24(0x7FFFFEu),
        ROW24(0x03E000u),
        ROW24(0x03E000u),
        ROW24(0x03E000u),
        ROW24(0x03E000u),
        ROW24(0x7FFF00u),
        ROW24(0x7FFF00u),
        ROW24(0x7FFF00u),
        ROW24(0x7FFF00u)
    },
    { /* 5 */
        ROW24(0x1FFFFCu),
        ROW24(0x1FFFFCu),
        ROW24(0x1FFFFCu),
        ROW24(0x1FFFFCu),
        ROW24(0x00007Cu),
        ROW24(0x00007Cu),
        ROW24(0x00007Cu),
        ROW24(0x00007Cu),
        ROW24(0x00007Cu),
        ROW24(0x00FC7Cu),
        ROW24(0x03FF7Cu),
        ROW24(0x0FFFFCu),
        ROW24(0x1FFFFCu),
        ROW24(0x1F83FCu),
        ROW24(0x3F00FCu),
        ROW24(0x3E007Cu),
        ROW24(0x7C0000u),
        ROW24(0x7C0000u),
        ROW24(0x7C0000u),
        ROW24(0x7C0000u),
        ROW24(0x7C0030u),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x3E007Cu),
        ROW24(0x3F00FCu),
        ROW24(0x1F81F8u),
        ROW24(0x0FFFF8u),
        ROW24(0x07FFF0u),
        ROW24(0x01FFC0u),
        ROW24(0x007F00u)
    },
    { /* 6 */
        ROW24(0x01FC00u),
        ROW24(0x0FFF00u),
        ROW24(0x1FFFC0u),
        ROW24(0x1FFFE0u),
        ROW24(0x0E03F0u),
        ROW24(0x0401F8u),
        ROW24(0x0000F8u),
        ROW24(0x00007Cu),
        ROW24(0x00003Cu),
        ROW24(0x00FC3Cu),
        ROW24(0x03FF3Eu),
        ROW24(0x0FFFBEu),
        ROW24(0x1FFFDEu),
        ROW24(0x1F81FEu),
        ROW24(0x3F00FEu),
        ROW24(0x3E007Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Cu),
        ROW24(0x7C003Cu),
        ROW24(0x3E007Cu),
        ROW24(0x3F00F8u),
        ROW24(0x1F81F8u),
        ROW24(0x1FFFF0u),
        ROW24(0x0FFFE0u),
        ROW24(0x03FFC0u),
        ROW24(0x00FE00u)
    },
    { /* 7 */
        ROW24(0x3FFFFEu),
        ROW24(0x3FFFFEu),
        ROW24(0x3FFFFEu),
        ROW24(0x3FFFFEu),
        ROW24(0x1E001Eu),
        ROW24(0x1F001Eu),
        ROW24(0x0F801Eu),
        ROW24(0x07801Eu),
        ROW24(0x03C01Eu),
        ROW24(0x01E01Eu),
        ROW24(0x01E000u),
        ROW24(0x00F000u),
        ROW24(0x00F800u),
        ROW24(0x007800u),
        ROW24(0x007C00u),
        ROW24(0x003C00u),
        ROW24(0x003C00u),
        ROW24(0x003E00u),
        ROW24(0x003E00u),
        ROW24(0x001E00u),
        ROW24(0x001E00u),
        ROW24(0x001F00u),
        ROW24(0x001F00u),
        ROW24(0x001F00u),
        ROW24(0x001F00u),
        ROW24(0x001F00u),
        ROW24(0x001F00u),
        ROW24(0x001F00u),
        ROW24(0x001F00u),
        ROW24(0x001F00u)
    },
    { /* 8 */
        ROW24(0x007F00u),
        ROW24(0x03FFC0u),
        ROW24(0x0FFFF0u),
        ROW24(0x0FFFF8u),
        ROW24(0x1F81F8u),
        ROW24(0x1F00FCu),
        ROW24(0x3E007Cu),
        ROW24(0x3E007Cu),
        ROW24(0x3E007Cu),
        ROW24(0x3E007Cu),
        ROW24(0x1F00F8u),
        ROW24(0x0F81F8u),
        ROW24(0x0FFFF0u),
        ROW24(0x03FFC0u),
        ROW24(0x03FFE0u),
        ROW24(0x0FFFF0u),
        ROW24(0x1F81F8u),
        ROW24(0x3E007Cu),
        ROW24(0x3C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7C003Eu),
        ROW24(0x3E007Eu),
        ROW24(0x3F81FCu),
        ROW24(0x1FFFF8u),
        ROW24(0x0FFFF0u),
        ROW24(0x07FFE0u),
        ROW24(0x00FF00u)
    },
    { /* 9 */
        ROW24(0x007F00u),
        ROW24(0x01FFC0u),
        ROW24(0x07FFF0u),
        ROW24(0x0FFFF8u),
        ROW24(0x0FC1FCu),
        ROW24(0x1F007Cu),
        ROW24(0x3E003Eu),
        ROW24(0x3E003Eu),
        ROW24(0x3C003Eu),
        ROW24(0x3C001Eu),
        ROW24(0x7C001Eu),
        ROW24(0x7C001Eu),
        ROW24(0x7C003Eu),
        ROW24(0x7E003Eu),
        ROW24(0x7E003Eu),
        ROW24(0x7F007Cu),
        ROW24(0x7FC1FCu),
        ROW24(0x7DFFF8u),
        ROW24(0x7DFFF0u),
        ROW24(0x3CFFC0u),
        ROW24(0x3C3F00u),
        ROW24(0x3E0000u),
        ROW24(0x3E0000u),
        ROW24(0x1F0000u),
        ROW24(0x0F8000u),
        ROW24(0x0FE070u),
        ROW24(0x07FFF8u),
        ROW24(0x03FFF8u),
        ROW24(0x00FFF0u),
        ROW24(0x003F80u)
    }
};
static const uint8_t dot_rows[6] = {
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu
};

static const uint8_t colon_rows[22] = {
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x00u,
    0x00u,
    0x00u,
    0x00u,
    0x00u,
    0x00u,
    0x00u,
    0x00u,
    0x00u,
    0x00u,
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu,
    0x3Fu
};

static const uint32_t dash_rows[4] = {
    0x3FFFFFu,
    0x3FFFFFu,
    0x3FFFFFu,
    0x3FFFFFu
};

#undef ROW24

bool font_large_has_glyph(char glyph)
{
    return (glyph >= '0' && glyph <= '9') || glyph == ':' ||
        glyph == '.' || glyph == '-' || glyph == ' ';
}

uint8_t font_large_glyph_width(char glyph)
{
    if (glyph == ' ') return FONT_LARGE_SPACE_WIDTH;
    return FONT_LARGE_CELL_WIDTH;
}

bool font_large_pixel(char glyph, uint8_t x, uint8_t y)
{
    if (x >= FONT_LARGE_CELL_WIDTH || y >= FONT_LARGE_CELL_HEIGHT) return false;
    if (glyph >= '0' && glyph <= '9') {
        const uint8_t row = (uint8_t)(y - 2u);
        if (row >= FONT_LARGE_CELL_HEIGHT - 3u) return false;
        return (digit_rows[(uint8_t)(glyph - '0')][row][x / 8u] &
            (uint8_t)(1u << (x % 8u))) != 0u;
    }
    if (glyph == '.' && x >= DOT_LEFT && x < DOT_LEFT + 6u &&
        y >= DOT_TOP && y < DOT_TOP + 6u) {
        return (dot_rows[y - DOT_TOP] &
            (uint8_t)(1u << (x - DOT_LEFT))) != 0u;
    }
    if (glyph == ':' && x >= COLON_LEFT && x < COLON_LEFT + 6u &&
        y >= COLON_TOP && y < COLON_TOP + 22u) {
        return (colon_rows[y - COLON_TOP] &
            (uint8_t)(1u << (x - COLON_LEFT))) != 0u;
    }
    if (glyph == '-' && x >= DASH_LEFT && x < DASH_LEFT + 22u &&
        y >= DASH_TOP && y < DASH_TOP + 4u) {
        return (dash_rows[y - DASH_TOP] &
            (uint32_t)(1u << (x - DASH_LEFT))) != 0u;
    }
    return false;
}
