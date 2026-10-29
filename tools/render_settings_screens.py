"""Render representative settings screens using the firmware bitmap font."""

from collections import defaultdict
from pathlib import Path
import re
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "renders" / "screens"
FONT = (ROOT / "src" / "font_small.c").read_text(encoding="utf-8")
THEME = (ROOT / "src" / "lcd_theme.h").read_text(encoding="utf-8")
GLYPHS = "0123456789:. %+-/<>?ABCDEFGHIJKLMNOPQRSTUVWXYZdhmnpsv"
FONT_ROWS = re.search(r"small_rows\[.*?\]\[7\]\s*=\s*\{(.*?)\n\};", FONT, re.S).group(1)
ROWS = re.findall(r"\{\s*((?:\d+\s*,\s*){6}\d+)\s*\}", FONT_ROWS)
BITMAPS = {glyph: tuple(map(int, row.split(','))) for glyph, row in zip(GLYPHS, ROWS)}


def theme_color(name):
    value = int(re.search(rf"#define LCD_COLOR_{name} 0x([0-9A-Fa-f]+)u", THEME).group(1), 16)
    r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
    return f"#{(r << 3) | (r >> 2):02X}{(g << 2) | (g >> 4):02X}{(b << 3) | (b >> 2):02X}"


BACKGROUND, TEXT, ACCENT, RED, YELLOW = (
    theme_color(name) for name in ("BACKGROUND", "TEXT", "ACCENT", "RED", "YELLOW")
)


def settings_line(label, value="", selected=False):
    line = [" "] * 20
    line[0] = ">" if selected else " "
    for i, glyph in enumerate(label[:13], 1):
        line[i] = glyph
    value = value[:9]
    start = max(20 - len(value), min(len(label), 13) + 2)
    for i, glyph in enumerate(value):
        if start + i < 20:
            line[start + i] = glyph
    return "".join(line)


def option_line(left, right="", selected_column=None):
    line = [" "] * 20
    for column, value in enumerate((left, right)):
        if not value:
            continue
        start = column * 10
        line[start] = ">" if selected_column == column else " "
        for i, glyph in enumerate(value[:8]):
            line[start + 1 + i] = glyph
    return "".join(line)


def render(filename, title, description, lines, colors):
    paths = defaultdict(list)
    for row, line in enumerate(lines):
        assert len(line) <= 20
        for column, glyph in enumerate(line):
            color = colors[row][column] if isinstance(colors[row], list) else colors[row]
            for y, bits in enumerate(BITMAPS[glyph]):
                for x in range(5):
                    if bits & (1 << (4 - x)):
                        px, py = column * 8 + x + 1, 3 + row * 12 + y + 1
                        if px < 159:
                            paths[color].append(f"M{px} {py}h1v1h-1z")
    content = [
        "<?xml version='1.0' encoding='utf-8'?>",
        '<svg xmlns="http://www.w3.org/2000/svg" width="640" height="320" viewBox="0 0 160 80" shape-rendering="crispEdges" role="img" aria-labelledby="title desc">',
        f'<title id="title">{escape(title)}</title>',
        f'<desc id="desc">{escape(description)}</desc>',
        f'<rect width="160" height="80" fill="{BACKGROUND}"/>',
    ]
    content.extend(f'<path fill="{color}" d="{"".join(pixels)}"/>' for color, pixels in paths.items())
    content.append("</svg>")
    (OUTPUT / filename).write_text("\n".join(content) + "\n", encoding="utf-8", newline="\n")


render("1.2.1-settings-simple.svg", "Settings - DOSE PERIOD selected",
       "The settings list starts with DOSE PERIOD, selected in the accent color, followed by LED, CLICKS and the rate alarm settings.",
       [settings_line("DOSE PERIOD", "DAY", True), settings_line("LED", "ALWAYS"),
        settings_line("CLICKS", "OFF"), settings_line("RATE SCREEN", "OFF"),
        settings_line("RATE LIGHT", "OFF"), settings_line("RATE SOUND", "OFF")],
       [ACCENT, TEXT, TEXT, TEXT, TEXT, TEXT])

option_colors = [TEXT] * 20
option_colors[:9] = [ACCENT] * 9
render("1.2.2-settings-many-options.svg", "Settings - LED BRIGHT options",
       "LED BRIGHT opens a separate screen with options in two columns, ordered down the left column and then down the right. AUTO is selected in the accent color.",
       ["LED BRIGHT", option_line("AUTO", "25%", 0), option_line("1%", "50%"),
        option_line("3%", "80%"), option_line("5%", "100%"), option_line("10%")],
       [TEXT, option_colors, TEXT, TEXT, TEXT, TEXT])

render("1.2.3-settings-actions.svg", "Settings - bottom of list",
       "Example with hidden settings enabled: RESET is selected in the accent color, LOG MEMORY is red, and POWER OFF and REBOOT are yellow.",
       [settings_line("LEAD ZERO", "SHOW"), settings_line("HV", "ON"),
        settings_line("LOG MEMORY", "EEPROM"), settings_line("RESET", "SETTINGS", True),
        settings_line("POWER OFF"), settings_line("REBOOT")],
       [TEXT, TEXT, RED, ACCENT, YELLOW, YELLOW])
