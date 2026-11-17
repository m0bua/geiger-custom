#!/usr/bin/env python3
"""Render the three device Debug pages from the firmware bitmap fonts."""

from collections import defaultdict
from pathlib import Path
import re
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "renders" / "screens"
SMALL_SOURCE = (ROOT / "src" / "font_small.c").read_text(encoding="utf-8")
LARGE_SOURCE = (ROOT / "src" / "font_large.c").read_text(encoding="utf-8")
BOARD_SOURCE = (ROOT / "src" / "board_stm32l051.c").read_text(encoding="utf-8")

SMALL_GLYPHS = "0123456789:. %-/<>?ABCDEFGHIKLMNOPRSTUVWXYZdhmnpsv\x01\x02\u00b5"
small_match = re.search(
    r"small_columns\[SMALL_GLYPH_COUNT\]\[5\]\s*=\s*\{(.*?)\n\};",
    SMALL_SOURCE,
    re.S,
)
if not small_match:
    raise ValueError("Could not find small font bitmap")
SMALL_COLUMNS = [
    [int(value) for value in re.findall(r"\d+", body)]
    for body in re.findall(r"\{([^{}]+)\}", small_match.group(1))
]
if len(SMALL_COLUMNS) != len(SMALL_GLYPHS):
    raise ValueError("Small font glyph table does not match its glyph list")
SMALL = dict(zip(SMALL_GLYPHS, SMALL_COLUMNS))

digit_match = re.search(r"digit_rows\[10\].*?=\s*\{(.*?)\n\};", LARGE_SOURCE, re.S)
if not digit_match:
    raise ValueError("Could not find large digit bitmap")
digit_blocks = re.findall(r"\{\s*/\* ([0-9]) \*/(.*?)\n\s*\}", digit_match.group(1), re.S)
LARGE_DIGITS = {
    digit: [int(value, 16) for value in re.findall(r"ROW24\(0x([0-9A-Fa-f]+)u\)", body)]
    for digit, body in digit_blocks
}
colon_match = re.search(r"colon_rows\[22\]\s*=\s*\{(.*?)\n\};", LARGE_SOURCE, re.S)
COLON = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)u", colon_match.group(1))]


def palette_color(name):
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)u", re.search(
        r"uint16_t lcd_theme_palette\[LCD_THEME_COLOR_COUNT\]\s*=\s*\{(.*?)\};",
        BOARD_SOURCE,
        re.S,
    ).group(1))]
    index = {"BACKGROUND": 0, "TEXT": 1, "ACCENT": 2}[name]
    value = values[index]
    red, green, blue = (value >> 11) & 31, (value >> 5) & 63, value & 31
    return f"#{(red << 3) | (red >> 2):02X}{(green << 2) | (green >> 4):02X}{(blue << 3) | (blue >> 2):02X}"


BACKGROUND = palette_color("BACKGROUND")
TEXT = palette_color("TEXT")
ACCENT = palette_color("ACCENT")


def small_pixel(glyph, x, y):
    columns = SMALL.get(glyph)
    return columns is not None and 0 < x < 6 and 0 < y < 9 and bool(columns[x - 1] & (1 << (y - 1)))


def large_pixel(glyph, x, y):
    if glyph.isdigit():
        rows = LARGE_DIGITS[glyph]
        row = y - 2
        return 0 <= row < len(rows) and bool(rows[row] & (1 << x))
    if glyph == ":":
        row, col = y - 5, x - 9
        return 0 <= row < len(COLON) and 0 <= col < 6 and bool(COLON[row] & (1 << col))
    return False


def svg_screen(filename, title, description, pixels):
    grouped = defaultdict(list)
    for x, y, color in pixels:
        if 0 <= x < 160 and 0 <= y < 80:
            grouped[color].append(f"M{x} {y}h1v1h-1z")
    content = [
        "<?xml version='1.0' encoding='utf-8'?>",
        '<svg xmlns="http://www.w3.org/2000/svg" width="640" height="320" viewBox="0 0 160 80" shape-rendering="crispEdges" role="img" aria-labelledby="title desc">',
        f'<title id="title">{escape(title)}</title>',
        f'<desc id="desc">{escape(description)}</desc>',
        f'<rect width="160" height="80" fill="{BACKGROUND}"/>',
    ]
    content.extend(f'<path fill="{color}" d="{"".join(points)}"/>' for color, points in grouped.items())
    content.append("</svg>")
    (OUTPUT / filename).write_text("\n".join(content) + "\n", encoding="utf-8", newline="\n")


def draw_small_line(pixels, text, x, y, color=TEXT):
    for index, glyph in enumerate(text):
        for gy in range(10):
            for gx in range(7):
                if small_pixel(glyph, gx, gy):
                    pixels.append((x + index * 8 + gx, y + gy, color))


def render_clock():
    pixels = []
    # Firmware places the date in cells 5..14 of the centered 20-cell row.
    draw_small_line(pixels, "     2026-11-16     ", 0, 2)
    for index, glyph in enumerate("12:45"):
        for y in range(33):
            for x in range(24):
                if large_pixel(glyph, x, y):
                    pixels.append((20 + index * 24 + x, 21 + y, TEXT))
    # Matches the right-aligned `123456 CNT ` value in the firmware clock page.
    draw_small_line(pixels, "         123456 CNT ", 0, 62)
    svg_screen("1.1.1-debug-clock.svg", "Debug - clock", "Debug clock page with date, large HH:MM time, and the pulse counter.", pixels)


def render_power():
    pixels = []
    lines = []
    line = list(" " * 20)
    line[:3] = "SOL"
    line[11:14] = "75%"       # Percent occupies cells 11..13 (end=14).
    line[17:20] = "987"       # Raw solar ADC is right-aligned in the last three cells.
    lines.append("".join(line))
    for label, shown, extra in (("BATTERY", "82%", "412"), ("B START", "96%", "487")):
        line = list(" " * 20)
        line[:len(label)] = label
        line[12:15] = shown      # Battery format is right-aligned to cell 15.
        line[17:20] = extra      # Raw ADC / percent is right-aligned to cell 20.
        lines.append("".join(line))
    for label, value in (("WAKE", "100% 0000:12"), ("SLEEP", "000% 0000:00"), ("UPTIME", "0D 00:12:45")):
        line = list(" " * 20)
        line[:len(label)] = label
        line[20-len(value):] = value
        lines.append("".join(line))
    for row, line in enumerate(lines):
        y = 2 + row * 12
        draw_small_line(pixels, line, 0, y)
    svg_screen("1.1.2-debug-power.svg", "Debug - power and uptime", "Debug power page showing solar, battery format, raw ADC values, wake and sleep time, and uptime.", pixels)


def render_logs():
    pixels = []
    lines = [("MIN", "8"), ("HOUR", "145"), ("DAY", "12"), ("ALERT", "2"), ("SPI", "1 MB"), ("STORE", "OK")]
    for row, (label, value) in enumerate(lines):
        line = label.ljust(20 - len(value)) + value
        draw_small_line(pixels, line, 0, 2 + row * 12)
    svg_screen("1.1.3-debug-logs.svg", "Debug - log storage", "Debug log page with physical record counts for each log, SPI size, and storage status.", pixels)


OUTPUT.mkdir(parents=True, exist_ok=True)
render_clock()
render_power()
render_logs()
