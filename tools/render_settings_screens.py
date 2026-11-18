"""Render representative settings screens using the firmware bitmap font."""

from collections import defaultdict
from pathlib import Path
import re
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "renders" / "screens"
FONT = (ROOT / "src" / "font_small.c").read_text(encoding="utf-8")
BOARD = (ROOT / "src" / "board_stm32l051.c").read_text(encoding="utf-8")
GLYPHS = "0123456789:. %-/<>?ABCDEFGHIKLMNOPRSTUVWXYZdhmnpsv"
FONT_COLUMNS = re.search(
    r"small_columns\[SMALL_GLYPH_COUNT\]\[5\]\s*=\s*\{(.*?)\n\};",
    FONT, re.S,
).group(1)
COLUMNS = [
    [int(value) for value in re.findall(r"\d+", body)]
    for body in re.findall(r"\{([^{}]+)\}", FONT_COLUMNS)
]
BITMAPS = dict(zip(GLYPHS, COLUMNS))


def theme_color(name):
    palette = re.search(
        r"uint16_t lcd_theme_palette\[LCD_THEME_COLOR_COUNT\]\s*=\s*\{(.*?)\};",
        BOARD, re.S,
    ).group(1)
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)u", palette)]
    index = {"BACKGROUND": 0, "TEXT": 1, "ACCENT": 2, "GREEN": 4, "YELLOW": 5, "RED": 6}[name]
    value = values[index]
    r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
    return f"#{(r << 3) | (r >> 2):02X}{(g << 2) | (g >> 4):02X}{(b << 3) | (b >> 2):02X}"


BACKGROUND, TEXT, ACCENT, RED, YELLOW, GREEN = (
    theme_color(name) for name in ("BACKGROUND", "TEXT", "ACCENT", "RED", "YELLOW", "GREEN")
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
            for x, bits in enumerate(BITMAPS[glyph]):
                for y in range(1, 9):
                    if bits & (1 << (y - 1)):
                        px, py = column * 8 + x + 1, 3 + row * 12 + y
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


render("1.2.1-settings-simple.svg", "Settings - LED selected",
       "The settings list shows DOSE PERIOD and RATE ACCURACY before LED, selected in the third row in the accent color with its current value ALWAYS, followed by CLICKS and the rate alarm settings.",
       [settings_line("DOSE PERIOD", "DAY"), settings_line("RATE ACCURACY", "30%"),
        settings_line("LED", "ALWAYS", True),
        settings_line("CLICKS", "OFF"), settings_line("RATE SCREEN", "OFF"),
        settings_line("RATE LIGHT", "OFF")],
       [TEXT, TEXT, ACCENT, TEXT, TEXT, TEXT])

option_colors = [[TEXT] * 20 for _ in range(3)]
option_colors[2][:9] = [GREEN] * 9
render("1.2.2-settings-many-options.svg", "Settings - LED options",
       "The LED setting opens its six choices in two columns, ordered down the left column and then down the right. The cursor and selected ALWAYS option are green.",
       ["LED", option_line("OFF", "YELLOW"), option_line("ON", "RED"),
        option_line("ALWAYS", "CUSTOM", 0)],
       [TEXT, *option_colors])

render("1.2.3-settings-actions.svg", "Settings - lower list",
       "Lower settings list with UNIT, RATE PREFIX, DOSE PREFIX, LEAD ZERO, and HV before POWER OFF. LEAD ZERO is selected.",
       [settings_line("UNIT", "SV"), settings_line("RATE PREFIX", "m"),
        settings_line("DOSE PREF", "m"), settings_line("LEAD ZERO", "SHOW", True),
        settings_line("HV", "ON"), settings_line("POWER OFF")],
       [TEXT, TEXT, TEXT, ACCENT, TEXT, YELLOW])
