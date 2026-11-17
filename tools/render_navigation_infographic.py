#!/usr/bin/env python3
"""Build the self-contained English navigation infographic from device renders."""

import base64
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
SCREENS = ROOT / "docs" / "renders" / "screens"
OUTPUT = ROOT / "docs" / "renders" / "device-navigation.svg"
WIDTH, HEIGHT = 1600, 1140
CARD, BORDER = "#111d23", "#293b44"
TEXT, MUTED = "#e7eef2", "#a5b5bd"
SHORT_L, SHORT_R = "#73ff00", "#43a7ff"
LONG_L, LONG_R = "#ffd34d", "#d779ff"

parts = [
    '<?xml version="1.0" encoding="utf-8"?>',
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" role="img" aria-labelledby="title desc">',
    '<title id="title">Geiger-2 — Interface Navigation</title>',
    '<desc id="desc">A guide to Rate and Dose, Debug and Settings, graphs, and all four short and long button actions.</desc>',
    '<defs>',
    f'<marker id="short-left" markerUnits="userSpaceOnUse" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto-start-reverse"><path d="M0 0L12 6L0 12Z" fill="{SHORT_L}"/></marker>',
    f'<marker id="short-right" markerUnits="userSpaceOnUse" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto"><path d="M0 0L12 6L0 12Z" fill="{SHORT_R}"/></marker>',
    f'<marker id="short-right-start" markerUnits="userSpaceOnUse" markerWidth="12" markerHeight="12" refX="2" refY="6" orient="auto-start-reverse"><path d="M0 0L12 6L0 12Z" fill="{SHORT_R}"/></marker>',
    f'<marker id="long-left" markerUnits="userSpaceOnUse" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto"><path d="M0 0L12 6L0 12Z" fill="{LONG_L}"/></marker>',
    f'<marker id="long-right" markerUnits="userSpaceOnUse" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto"><path d="M0 0L12 6L0 12Z" fill="{LONG_R}"/></marker>',
    '</defs>',
    '<g transform="translate(0 -60)">',
]


def text(x, y, value, size=18, color=TEXT, weight=400, anchor="start",
         stroke=None, stroke_width=0):
    outline = f' stroke="{stroke}" stroke-width="{stroke_width}" stroke-linejoin="round" paint-order="stroke"' if stroke else ""
    parts.append(f'<text x="{x}" y="{y}" fill="{color}" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}"{outline}>{escape(value)}</text>')


def control_text(x, y, press, button, action, size=16, color=MUTED):
    button_color = SHORT_L if button == "L" else SHORT_R
    parts.append(f'<text x="{x}" y="{y}" fill="{color}" font-family="Inter,Segoe UI,Arial,sans-serif" font-size="{size}">{escape(press)} <tspan fill="{button_color}" font-weight="700">{button}</tspan> {escape(action)}</text>')


def rect(x, y, width, height, fill=CARD, stroke=BORDER, radius=18):
    parts.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{radius}" fill="{fill}" fill-opacity=".72" stroke="{stroke}" stroke-width="2"/>')


def image(x, y, width, height, filename):
    data = base64.b64encode((SCREENS / filename).read_bytes()).decode("ascii")
    parts.append(f'<image x="{x}" y="{y}" width="{width}" height="{height}" href="data:image/svg+xml;base64,{data}" preserveAspectRatio="none"/>')


def arrow(x1, y1, x2, y2, color, marker, width=4, start_marker=None):
    start = f' marker-start="url(#{start_marker})"' if start_marker else ""
    parts.append(f'<path d="M{x1} {y1}L{x2} {y2}" fill="none" stroke="{color}" stroke-width="{width}"{start} marker-end="url(#{marker})"/>')


def routed_arrow(path, color, marker, width=3):
    parts.append(f'<path d="{path}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round" marker-end="url(#{marker})"/>')


def pill(x, y, label, color, width=92, height=34):
    parts.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{height/2:g}" fill="{color}" fill-opacity=".13" stroke="{color}" stroke-opacity=".75"/>')
    text(x + width / 2, y + height * .68, label, 14, color, 700, "middle")


def vertical_pill(cx, cy, label, color, width=64, height=22):
    parts.append(f'<g transform="rotate(-90 {cx} {cy})">')
    pill(cx - width / 2, cy - height / 2, label, color, width, height)
    parts.append('</g>')


# Heading.
text(64, 126, "Interface Navigation", 46, TEXT, 700,
     stroke="#000000", stroke_width=2.5)

# Main screens.
rect(40, 160, 700, 260)
rect(860, 160, 700, 260)
parts.append('<g transform="translate(0 -32)">')
text(68, 230, "RATE", 21, SHORT_L, 750)
text(888, 230, "DOSE", 21, SHORT_R, 750)
image(66, 252, 350, 175, "1.0.2-rate-green-cpm.svg")
image(886, 252, 350, 175, "2-dose.svg")
pill(460, 250, "SHORT L", SHORT_L)
text(460, 306, "NORMAL", 13, MUTED, 600)
arrow(520, 302, 542, 302, SHORT_L, "short-left", 3)
text(552, 306, "SEARCH", 13, MUTED, 600)
arrow(608, 302, 630, 302, SHORT_L, "short-left", 3)
text(640, 306, "INVIS", 13, MUTED, 600)
routed_arrow("M656 314V336H485V314", SHORT_L, "short-left", 3)
pill(460, 362, "LONG L", LONG_L)
text(460, 418, "Flashlight", 17)
text(540, 418, "ON", 12, MUTED, 600)
arrow(565, 414, 600, 414, LONG_L, "long-left", 2)
arrow(600, 414, 565, 414, LONG_L, "long-left", 2)
text(608, 418, "OFF", 12, MUTED, 600)
pill(1260, 250, "SHORT L", SHORT_L)
text(1260, 306, "NORMAL", 13, MUTED, 600)
arrow(1320, 302, 1342, 302, SHORT_L, "short-left", 3)
text(1352, 306, "SEARCH", 13, MUTED, 600)
arrow(1408, 302, 1430, 302, SHORT_L, "short-left", 3)
text(1440, 306, "INVIS", 13, MUTED, 600)
routed_arrow("M1456 314V336H1285V314", SHORT_L, "short-left", 3)
pill(1260, 362, "LONG L", LONG_L)
text(1260, 418, "Flashlight", 17)
text(1340, 418, "ON", 12, MUTED, 600)
arrow(1365, 414, 1400, 414, LONG_L, "long-left", 2)
arrow(1400, 414, 1365, 414, LONG_L, "long-left", 2)
text(1408, 418, "OFF", 12, MUTED, 600)
# Rate/Dose navigation uses the same paired short-button arrows as Move Cursor.
pill(762, 294, "SHORT R", SHORT_R, 76, 26)
arrow(764, 329, 836, 329, SHORT_R, "short-right", 3)
arrow(836, 341, 764, 341, SHORT_L, "short-left", 3)
pill(762, 352, "SHORT L", SHORT_L, 76, 26)
parts.append('</g>')

# Paired vertical long-button navigation at the main-screen transitions.
parts.append('<g transform="translate(0 -100)">')
arrow(210, 535, 210, 583, LONG_R, "long-right", 4)
arrow(224, 583, 224, 535, LONG_L, "long-left", 4)
pill(106, 544, "LONG R", LONG_R)
pill(240, 544, "LONG L", LONG_L)
arrow(530, 583, 530, 535, LONG_L, "long-left", 4)
pill(556, 544, "LONG L", LONG_L)
arrow(1313, 535, 1313, 583, LONG_R, "long-right", 4)
arrow(1327, 583, 1327, 535, LONG_L, "long-left", 4)
pill(1209, 544, "LONG R", LONG_R)
pill(1343, 544, "LONG L", LONG_L)

# Three equal vertical panels: Debug, Settings, and Graphs.
rect(40, 600, 340, 560)
rect(440, 600, 560, 560)
rect(1080, 600, 480, 560)

# Debug pages are stacked vertically and cycle Clock → Power → Logs → Clock.
text(68, 642, "DEBUG", 22, TEXT, 750)
pill(68, 656, "LONG R", LONG_R, 90, 28)
text(170, 677, "switches screens", 16, MUTED)
debug_items = [
    ("CLOCK", "1.1.1-debug-clock.svg", 728),
    ("POWER", "1.1.2-debug-power.svg", 884),
    ("LOGS / SPI", "1.1.3-debug-logs.svg", 1040),
]
for label, filename, y in debug_items:
    text(90, y - 6, label, 12, MUTED, 700)
    image(90, y, 180, 90, filename)
# Long R advances down the Debug pages: Clock → Power → Logs / SPI.
arrow(180, 820, 180, 872, LONG_R, "long-right", 3)
pill(198, 833, "LONG R", LONG_R, 78, 26)
arrow(180, 976, 180, 1028, LONG_R, "long-right", 3)
pill(198, 989, "LONG R", LONG_R, 78, 26)
# From Logs / SPI, Long R returns to Clock along a routed path to the right
# of the renders, then points left into the first page.
parts.append(f'<path d="M270 1085L306 1085L306 774L270 774" fill="none" stroke="{LONG_R}" stroke-width="3" marker-end="url(#long-right)"/>')
vertical_pill(330, 929, "LONG R", LONG_R)

# Settings screens are stacked in menu order with their key actions explained.
text(468, 642, "SETTINGS", 22, TEXT, 750)
pill(468, 656, "LONG R", LONG_R, 90, 28)
text(570, 677, "activates the cursor", 16, MUTED)
pill(468, 690, "LONG R", LONG_R, 90, 28)
text(570, 711, "again — change setting", 16, MUTED)
# Menu and Options share the top row; Actions sits centered below them.
image(460, 750, 210, 105, "1.2.1-settings-simple.svg")
image(770, 750, 210, 105, "1.2.2-settings-many-options.svg")
image(460, 1028, 210, 105, "1.2.3-settings-actions.svg")
# Long presses move between Menu and Options; short presses connect to Actions.
arrow(675, 795.5, 765, 795.5, LONG_R, "long-right", 3)
arrow(765, 809.5, 675, 809.5, LONG_L, "long-left", 3)
pill(681, 763, "LONG R", LONG_R, 78, 24)
pill(681, 818, "LONG L", LONG_L, 78, 24)
arrow(558, 868, 558, 1013, SHORT_R, "short-right", 3)
arrow(572, 1015, 572, 870, SHORT_L, "short-left", 3)
vertical_pill(528, 929, "SHORT R", SHORT_R)
vertical_pill(603, 929, "SHORT L", SHORT_L)

# Actions for the selected setting. Blue points left at the end of its elbow;
# the shorter green route runs inside and alongside it, pointing upward.
routed_arrow("M940 868V988H880", SHORT_R, "short-right")
routed_arrow("M880 944H920V868", SHORT_L, "short-left")
pill(784, 932, "SHORT L", SHORT_L, 78, 24)
pill(784, 976, "SHORT R", SHORT_R, 78, 24)
text(823, 1028, "Move cursor", 12, MUTED, 400, "middle")

routed_arrow("M960 868V1108H880", LONG_R, "long-right")
text(740, 1113, "Select", 12, MUTED)
pill(784, 1096, "LONG R", LONG_R, 78, 24)

# Short navigation between the Debug and Settings columns.
arrow(388, 900, 432, 900, SHORT_R, "short-right", 3)
arrow(432, 916, 388, 916, SHORT_L, "short-left", 3)
vertical_pill(410, 850, "SHORT R", SHORT_R)
vertical_pill(410, 966, "SHORT L", SHORT_L)

# Graph column, entered from Dose.
parts.append('<g transform="translate(0 0)">')
text(1100, 642, "GRAPHS", 22, TEXT, 750)
pill(1100, 652, "LONG R", LONG_R, 88, 30)
text(1206, 674, "Open graph from Dose", 15, MUTED)
graph_items = ["2.1-graph-90s.svg", "2.2-graph-days.svg", "2.3-graph-alerts.svg"]
for i, (filename, label) in enumerate(zip(graph_items, ("DIRECT", "DAYS", "ALERTS"))):
    x = 1102 + i * 150
    image(x, 705, 135, 68, filename)
    text(x + 67, 795, label, 13, MUTED, 650, "middle")

# Short-button navigation is a vertical list. Each neighboring pair has its
# own arrows, so DIRECT → MIN is explicit; the names sit centered between them.
parts.append('<g transform="translate(40 -40)">')
nodes = (("DIRECT", 900), ("MIN", 970), ("HOUR", 1040), ("DAY", 1110), ("ALERT", 1180))
for label, y in nodes:
    text(1150, y, label, 14, TEXT, 700, "middle")
for (_, y0), (_, y1) in zip(nodes, nodes[1:]):
    arrow(1143.5, y0 + 10, 1143.5, y1 - 20, SHORT_R, "short-right", 3)
    arrow(1156.5, y1 - 20, 1156.5, y0 + 10, SHORT_L, "short-left", 3)

# Rotate the short-action labels to sit beside their matching vertical arrows.
parts.append('<g transform="rotate(-90 1093 1040)">')
pill(1054, 1027, "SHORT R", SHORT_R, 78, 26)
parts.append('</g>')
arrow(1120, 1172, 1120, 908, SHORT_R, "short-right", 3)
parts.append('<g transform="rotate(-90 1210 1040)">')
pill(1171, 1027, "SHORT L", SHORT_L, 78, 26)
parts.append('</g>')
arrow(1180, 908, 1180, 1172, SHORT_L, "short-left", 3)
parts.append('</g>')

# Long L is centered between two aligned rows. DIRECT and 90s share a baseline;
# each column pairs its larger and smaller time ranges.
parts.append('<g transform="translate(0 -40)">')
parts.append('<g transform="translate(-20 0)">')
text(1265, 900, "90s", 10, LONG_L, 600, "middle")
text(1320, 868, "3m", 10, LONG_L, 600, "middle")
text(1400, 868, "5m", 10, LONG_L, 600, "middle")
text(1480, 868, "10m", 10, LONG_L, 600, "middle")
text(1320, 940, "60s", 10, LONG_L, 600, "middle")
text(1400, 940, "30s", 10, LONG_L, 600, "middle")
text(1480, 940, "10s", 10, LONG_L, 600, "middle")
pill(1337, 887, "LONG L", LONG_L, 78, 26)
arrow(1278, 896, 1308, 866, LONG_L, "long-left", 3)
arrow(1339, 864, 1381, 864, LONG_L, "long-left", 3)
arrow(1418, 864, 1460, 864, LONG_L, "long-left", 3)
arrow(1480, 879, 1480, 921, LONG_L, "long-left", 3)
arrow(1461, 936, 1419, 936, LONG_L, "long-left", 3)
arrow(1381, 936, 1339, 936, LONG_L, "long-left", 3)
arrow(1308, 933, 1278, 903, LONG_L, "long-left", 3)
parts.append('</g>')
parts.append('</g>')

# One brace groups MIN/HOUR/DAY/ALERT, sharing the column-select action.
parts.append('<g transform="translate(8 -40)">')
parts.append(f'<path d="M1245 950C1265 950 1265 965 1265 990C1265 1015 1280 1040 1295 1060C1280 1080 1265 1105 1265 1130C1265 1155 1265 1170 1245 1170" fill="none" stroke="{MUTED}" stroke-width="4"/>')
parts.append('</g>')
parts.append('<g transform="translate(5 -40)">')
arrow(1310, 1054, 1378, 1054, LONG_R, "long-right", 3)
parts.append('<g transform="rotate(-90 1399 1059)">')
text(1399, 1059, "SELECT", 14, TEXT, 700, "middle")
parts.append('</g>')
pill(1305, 1016, "LONG R", LONG_R, 78, 26)
arrow(1378, 1067, 1310, 1067, LONG_L, "long-left", 3)
pill(1305, 1079, "LONG L", LONG_L, 78, 26)
pill(1417, 1016, "SHORT R", SHORT_R, 78, 26)
arrow(1422, 1054, 1490, 1054, SHORT_R, "short-right", 3)
arrow(1490, 1067, 1422, 1067, SHORT_L, "short-left", 3)
pill(1417, 1079, "SHORT L", SHORT_L, 78, 26)
parts.append('<g transform="rotate(-90 1491 1059)">')
text(1490, 1079, "MOVE CURSOR", 14, TEXT, 700, "middle")
parts.append('</g>')
parts.append('</g>')
parts.append('</g>')
# Four-action color key.
rect(40, 1175, 1520, 104, "#0f1b20", BORDER, 14)
text(68, 1217, "BUTTON ACTIONS", 14, MUTED, 700)
pill(240, 1197, "SHORT L", SHORT_L, 124)
pill(390, 1197, "SHORT R", SHORT_R, 124)
pill(540, 1197, "LONG L", LONG_L, 124)
pill(690, 1197, "LONG R", LONG_R, 124)
pill(870, 1197, "L", SHORT_L, 38)
text(918, 1220, "Left button", 15, MUTED)
pill(1045, 1197, "R", SHORT_R, 38)
text(1093, 1220, "Right button", 15, MUTED)
text(1260, 1220, "Arrow colors match actions", 15, MUTED)

parts.append('</g>')
parts.append('</g>')
parts.append("</svg>")
OUTPUT.write_text("\n".join(parts) + "\n", encoding="utf-8", newline="\n")
print(f"Wrote {OUTPUT.relative_to(ROOT)}")
