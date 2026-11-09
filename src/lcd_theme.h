#ifndef LCD_THEME_H
#define LCD_THEME_H

#include <stdint.h>

/* Editable base palette, stored in EEPROM and managed by the companion app. */
enum {
    LCD_THEME_BACKGROUND,
    LCD_THEME_TEXT,
    LCD_THEME_ACCENT,
    LCD_THEME_BLUE,
    LCD_THEME_GREEN,
    LCD_THEME_YELLOW,
    LCD_THEME_RED,
    LCD_THEME_COLOR_COUNT
};

extern uint16_t lcd_theme_palette[LCD_THEME_COLOR_COUNT];
extern uint8_t lcd_theme_graph_dimming_percent;

#define LCD_COLOR_BACKGROUND (lcd_theme_palette[LCD_THEME_BACKGROUND])
#define LCD_COLOR_TEXT (lcd_theme_palette[LCD_THEME_TEXT])
#define LCD_COLOR_ACCENT (lcd_theme_palette[LCD_THEME_ACCENT])
#define LCD_COLOR_BLUE (lcd_theme_palette[LCD_THEME_BLUE])
#define LCD_COLOR_GREEN (lcd_theme_palette[LCD_THEME_GREEN])
#define LCD_COLOR_YELLOW (lcd_theme_palette[LCD_THEME_YELLOW])
#define LCD_COLOR_RED (lcd_theme_palette[LCD_THEME_RED])
#define LCD_COLOR_PROGRESS_FRAME LCD_COLOR_BLUE
#define LCD_COLOR_PROGRESS_FILL LCD_COLOR_BLUE
#define LCD_COLOR_GRAPH 0xFFFFu
static inline uint16_t lcd_theme_dim(uint16_t color, uint8_t percent)
{
    const uint16_t brightness = percent >= 100u ? 0u : (uint16_t)(100u - percent);
    return (uint16_t)(((((color >> 11u) & 0x1Fu) * brightness / 100u) << 11u) |
        ((((color >> 5u) & 0x3Fu) * brightness / 100u) << 5u) |
        ((color & 0x1Fu) * brightness / 100u));
}

#define LCD_COLOR_MENU_ACTION lcd_theme_dim(LCD_COLOR_YELLOW, 30u)
#define LCD_COLOR_MENU_WARNING lcd_theme_dim(LCD_COLOR_RED, 30u)
#define LCD_COLOR_GRAPH_DIM lcd_theme_dim(LCD_COLOR_GRAPH, lcd_theme_graph_dimming_percent)

/* Semantic aliases used by the existing renderer. */
#define LCD_COUNTER_GREEN LCD_COLOR_GREEN
#define LCD_COUNTER_YELLOW LCD_COLOR_YELLOW
#define LCD_COUNTER_RED LCD_COLOR_RED
#define LCD_COUNTER_BLUE LCD_COLOR_BLUE
#define LCD_COUNTER_LIGHT_GRAY LCD_COLOR_TEXT
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
