#ifndef LCD_THEME_H
#define LCD_THEME_H

#include <stdint.h>

/* Screen theme palette (RGB565). Change these base colors to retheme the UI. */
#define LCD_COLOR_BACKGROUND 0x0000u
#define LCD_COLOR_TEXT 0xD6BAu
#define LCD_COLOR_ACCENT 0x07EEu
#define LCD_COLOR_BLUE 0x001Fu
#define LCD_COLOR_GREEN 0x07E0u
#define LCD_COLOR_YELLOW 0xFFE0u
#define LCD_COLOR_RED 0xF800u
#define LCD_COLOR_PROGRESS_FRAME LCD_COLOR_TEXT
#define LCD_COLOR_PROGRESS_FILL LCD_COLOR_TEXT
#define LCD_COLOR_GRAPH LCD_COLOR_TEXT
#define LCD_THEME_BACKGROUND_RGB_SUM \
    (((((LCD_COLOR_BACKGROUND) >> 11u) & 0x1Fu) * 255u / 31u) + \
    ((((LCD_COLOR_BACKGROUND) >> 5u) & 0x3Fu) * 255u / 63u) + \
    (((LCD_COLOR_BACKGROUND) & 0x1Fu) * 255u / 31u))
/* Mean RGB brightness above 50% selects the light-theme adjustment. */
#define LCD_THEME_LIGHT_BACKGROUND (LCD_THEME_BACKGROUND_RGB_SUM > 382u)

#if LCD_THEME_LIGHT_BACKGROUND
#define LCD_RGB565_ADJUST5_CHANNEL(value, maximum) \
    ((value) == 0u ? 0u : \
        (((((value) * 255u / (maximum)) + 5u) * (maximum)) / 255u))
#else
#define LCD_RGB565_ADJUST5_CHANNEL(value, maximum) \
    ((value) == 0u ? 0u : \
        (((value) * 255u / (maximum)) <= 5u ? 0u : \
        (((((value) * 255u / (maximum)) - 5u) * (maximum)) / 255u)))
#endif

#define LCD_RGB565_ADJUST5(color) \
    ((uint16_t)((LCD_RGB565_ADJUST5_CHANNEL(((color) >> 11u) & 0x1Fu, 31u) << 11u) | \
    (LCD_RGB565_ADJUST5_CHANNEL(((color) >> 5u) & 0x3Fu, 63u) << 5u) | \
    LCD_RGB565_ADJUST5_CHANNEL((color) & 0x1Fu, 31u)))

#define LCD_COLOR_MENU_ACTION LCD_RGB565_ADJUST5(LCD_COLOR_YELLOW)
#define LCD_COLOR_MENU_WARNING LCD_RGB565_ADJUST5(LCD_COLOR_RED)
#define LCD_COLOR_GRAPH_DIM LCD_RGB565_ADJUST5(LCD_COLOR_GRAPH)

/* Semantic aliases used by the existing renderer. */
#define LCD_COUNTER_GREEN LCD_COLOR_GREEN
#define LCD_COUNTER_YELLOW LCD_COLOR_YELLOW
#define LCD_COUNTER_RED LCD_COLOR_RED
#define LCD_COUNTER_BLUE LCD_COLOR_BLUE
#define LCD_COUNTER_LIGHT_GRAY LCD_COLOR_PROGRESS_FRAME
#define LCD_TEXT_FOREGROUND LCD_COLOR_TEXT
#define LCD_TEXT_BACKGROUND LCD_COLOR_BACKGROUND
#define LCD_TEXT_GREEN LCD_COLOR_GREEN
#define LCD_TEXT_YELLOW LCD_COLOR_YELLOW
#define LCD_TEXT_RED LCD_COLOR_RED
#define LCD_SETTINGS_SELECTED_COLOR LCD_COLOR_ACCENT
#define LCD_SETTINGS_ACTION_COLOR LCD_COLOR_MENU_ACTION
#define LCD_SETTINGS_WARNING_COLOR LCD_COLOR_MENU_WARNING
#define LCD_GRAPH_COLOR LCD_COLOR_GRAPH
#define LCD_GRAPH_DIM_COLOR LCD_COLOR_GRAPH_DIM

#endif
