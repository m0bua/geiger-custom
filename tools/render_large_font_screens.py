#!/usr/bin/env python3
"""Refresh the large numeric glyphs in the hand-authored screen SVG renders."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONT_SOURCE = (ROOT / "src/font_large.c").read_text(encoding="utf-8")
SCREEN_DIR = ROOT / "docs/renders/screens"


def rows_for(name: str) -> list[int]:
    match = re.search(
        rf"{name}\[\d+\]\s*=\s*\{{(.*?)\n\}};", FONT_SOURCE, re.S
    )
    if not match:
        raise ValueError(f"Could not find {name} in src/font_large.c")
    return [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)u?", match.group(1))]


digits_match = re.search(r"digit_rows\[10\].*?=\s*\{(.*?)\n\};", FONT_SOURCE, re.S)
if not digits_match:
    raise ValueError("Could not find digit_rows in src/font_large.c")
digit_blocks = re.findall(r"\{\s*/\* ([0-9]) \*/(.*?)\n\s*\}", digits_match.group(1), re.S)
if len(digit_blocks) != 10:
    raise ValueError("Unexpected digit_rows format")
DIGITS = {
    char: [int(value, 16) for value in re.findall(r"ROW24\(0x([0-9A-Fa-f]+)u\)", body)]
    for char, body in digit_blocks
}
DOT = rows_for("dot_rows")
if any(len(DIGITS[str(n)]) != 30 for n in range(10)) or len(DOT) != 6:
    raise ValueError("Unexpected large-font bitmap dimensions")


def pixel(glyph: str, x: int, y: int) -> bool:
    if glyph.isdigit():
        row = y - 2
        return 0 <= row < 30 and bool(DIGITS[glyph][row] & (1 << x))
    if glyph == ".":
        row, col = y - 26, x - 9
        return 0 <= row < 6 and 0 <= col < 6 and bool(DOT[row] & (1 << col))
    return False


def refresh(filename: str, text: str, color: str) -> None:
    path = SCREEN_DIR / filename
    svg = path.read_text(encoding="utf-8")
    existing_pixels = re.compile(r"M(\d+) (\d+)h1v1h-1z")
    tag_pattern = re.compile(r'<(?P<tag>(?:ns0:)?path) fill="(?P<fill>#[0-9A-Fa-f]{6})" d="(?P<d>[^"]*)"\s*/>')

    def remove_old_large_pixels(match: re.Match[str]) -> str:
        d = match.group("d")
        d = existing_pixels.sub(
            lambda p: "" if 8 <= int(p.group(1)) < 152 and 25 <= int(p.group(2)) <= 55 else p.group(0),
            d,
        )
        if not d:
            return ""
        return f'<{match.group("tag")} fill="{match.group("fill")}" d="{d}"/>'

    svg = tag_pattern.sub(remove_old_large_pixels, svg)
    # The six-cell fixed rate field is centered at x=8; five glyphs use slots 1–5.
    start_x = 8 + 24
    start_y = 23
    pixels = []
    for index, glyph in enumerate(text):
        for y in range(33):
            for x in range(24):
                if pixel(glyph, x, y):
                    pixels.append(f"M{start_x + index * 24 + x} {start_y + y}h1v1h-1z")
    prefix = "ns0:" if "<ns0:svg" in svg else ""
    overlay = f'<{prefix}path fill="{color}" d="{"".join(pixels)}"/>'
    svg = svg.replace("</ns0:svg>" if prefix else "</svg>", overlay + ("</ns0:svg>" if prefix else "</svg>"))
    path.write_text(svg, encoding="utf-8", newline="\n")


refresh("1.0.1-rate-blue-cnt.svg", "0.153", "#0000FF")
refresh("1.0.2-rate-green-cpm.svg", "0.153", "#00FF00")
refresh("2-dose.svg", "1.234", "#D6D6D6")
