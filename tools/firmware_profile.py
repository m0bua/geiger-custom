"""Validation and compiler flags for Quick Firmware build profiles."""

from __future__ import annotations

import json
from pathlib import Path


PROFILE_FORMAT = "geiger-custom-firmware-profile"
PROFILE_SCHEMA_VERSION = 3

FEATURE_MACROS = {
    "logs.enabled": "FW_FEATURE_LOGS",
    "device.graphs": "FW_FEATURE_DEVICE_GRAPHS",
    "device.debug": "FW_FEATURE_DEVICE_DEBUG",
    "indication.flashlight": "FW_FEATURE_FLASHLIGHT",
    "indication.modes": "FW_FEATURE_INDICATION_MODES",
    "alarms.rate": "FW_FEATURE_RATE_ALARMS",
    "alarms.dose": "FW_FEATURE_DOSE_ALARMS",
    "theme.editable": "FW_FEATURE_THEME_CONFIG",
    "settings.mutable": "FW_FEATURE_MUTABLE_SETTINGS",
}

DEFAULT_FEATURES = {key: True for key in FEATURE_MACROS}
SETTING_DEFS = {
    "led": (0, 6, 2), "clicks": (1, 6, 0), "rate-screen": (2, 4, 0),
    "rate-light": (3, 4, 0), "rate-sound": (4, 4, 0),
    "led-bright": (5, 9, 0), "screen-bright": (6, 9, 0),
    "screen-timer": (7, 6, 2), "lead-zero": (8, 2, 0), "unit": (9, 3, 0),
    "rate-prefix": (10, 4, 1), "dose-prefix": (11, 4, 1),
    "rate-row-2": (12, 6, 0), "log-memory": (13, 3, 2), "hv": (15, 2, 0),
    "rate-custom": (16, 9, 4), "alarm-stop": (17, 2, 0), "tone": (20, 2, 0),
    "long-press": (21, 6, 3), "dose-row-2": (22, 8, 1),
    "dose-period": (23, 4, 1), "dose-screen": (24, 4, 0),
    "dose-light": (25, 4, 0), "dose-sound": (26, 4, 0),
    "dose-custom": (27, 8, 0), "led-time": (28, 9, 0),
    "batt-format": (29, 3, 0), "led-color": (30, 2, 0),
    "rate-accuracy": (31, 6, 4),
}
THEME_PRESETS = {
    "dark": (0x0000, 0xD6BA, 0x77E0, 0x001F, 0x07E0, 0xFFE0, 0xF800),
    "light": (0xFFFF, 0x0000, 0x0320, 0x02D7, 0x0400, 0xBC21, 0xC145),
    "ocean": (0x0005, 0xC7D9, 0x06BF, 0x4E5E, 0x26B3, 0xFE8C, 0xFB4D),
    "amber": (0x0000, 0xFEF7, 0xFD20, 0x5D7C, 0x8DEC, 0xFD80, 0xF2C5),
    "violet": (0x1083, 0xE6DE, 0xDBDF, 0x7D1E, 0x8EB3, 0xE60F, 0xE36E),
    "monokai": (0x2144, 0xFFDE, 0xA705, 0x66DD, 0xA705, 0xE6CE, 0xF92E),
    "dracula": (0x2946, 0xFFDE, 0xBC9F, 0x8F5F, 0x57CF, 0xF7D1, 0xFAAA),
    "nord": (0x29A8, 0xDEFD, 0x8E1A, 0x8518, 0xA5F1, 0xEE51, 0xBB0D),
    "solarized": (0x0146, 0x84B2, 0x245A, 0x245A, 0x84C0, 0xB440, 0xD985),
}


def normalize_profile(value: object) -> dict:
    if not isinstance(value, dict):
        raise ValueError("Build profile must be a JSON object.")
    if value.get("format") != PROFILE_FORMAT:
        raise ValueError(f"Build profile format must be {PROFILE_FORMAT!r}.")
    version = value.get("schemaVersion", PROFILE_SCHEMA_VERSION)
    if isinstance(version, bool) or not isinstance(version, int) or version < 1:
        raise ValueError("schemaVersion must be a positive integer.")
    raw_features = value.get("features", {})
    if not isinstance(raw_features, dict):
        raise ValueError("features must be a JSON object.")

    features = dict(DEFAULT_FEATURES)
    for key in FEATURE_MACROS:
        if key in raw_features:
            if not isinstance(raw_features[key], bool):
                raise ValueError(f"features[{key!r}] must be true or false.")
            features[key] = raw_features[key]

    raw_settings = value.get("settings", {})
    if not isinstance(raw_settings, dict):
        raise ValueError("settings must be a JSON object.")
    legacy_mutable = features["settings.mutable"]
    settings = dict(raw_settings)
    for key, (_, option_count, default_value) in SETTING_DEFS.items():
        raw = raw_settings.get(key, {})
        if not isinstance(raw, dict):
            raise ValueError(f"settings[{key!r}] must be an object.")
        mutable = raw.get("mutable", legacy_mutable)
        selected = raw.get("default", default_value)
        if not isinstance(mutable, bool):
            raise ValueError(f"settings[{key!r}].mutable must be true or false.")
        if isinstance(selected, bool) or not isinstance(selected, int) or not 0 <= selected < option_count:
            raise ValueError(f"settings[{key!r}].default must be in 0..{option_count - 1}.")
        if key == "hv":
            if not mutable:
                selected = 0
        settings[key] = {**raw, "mutable": mutable, "default": selected}

    unit = settings["unit"]["default"]
    unit_mutable = settings["unit"]["mutable"]
    prefix_max = 2 if unit == 1 else 4
    if unit_mutable:
        prefix_max = 2
    for key in ("rate-prefix", "dose-prefix"):
        if settings[key]["default"] >= prefix_max:
            raise ValueError(f"settings[{key!r}] default is incompatible with the selected or mutable UNIT.")
    row2_max = 5 if unit != 1 else 6
    if unit_mutable:
        row2_max = 5
    if settings["rate-row-2"]["default"] >= row2_max:
        raise ValueError("settings['rate-row-2'] default is incompatible with the selected or mutable UNIT.")

    raw_theme = value.get("theme", {})
    if not isinstance(raw_theme, dict):
        raise ValueError("theme must be an object.")
    theme_mutable = raw_theme.get("mutable", features["theme.editable"])
    if not isinstance(theme_mutable, bool):
        raise ValueError("theme.mutable must be true or false.")
    theme_preset = raw_theme.get("preset", "dark")
    if theme_preset not in THEME_PRESETS and theme_preset != "custom":
        raise ValueError("theme.preset is not a supported theme.")
    colors = raw_theme.get("colors")
    if colors is None:
        colors = ["#%06X" % rgb565_to_rgb888(value)
                  for value in THEME_PRESETS.get(theme_preset, THEME_PRESETS["dark"])]
    if (not isinstance(colors, list) or len(colors) != 7 or
            any(not isinstance(color, str) or len(color) != 7 or color[0] != "#"
                or any(char not in "0123456789abcdefABCDEF" for char in color[1:])
                for color in colors)):
        raise ValueError("theme.colors must contain seven #RRGGBB colors.")
    dimming = raw_theme.get("dimming", 30)
    if isinstance(dimming, bool) or not isinstance(dimming, int) or not 0 <= dimming <= 100:
        raise ValueError("theme.dimming must be an integer in 0..100.")
    theme = {**raw_theme, "mutable": theme_mutable, "colors": colors,
             "dimming": dimming}
    features["settings.mutable"] = any(item["mutable"] for key, item in settings.items()
                                       if key in SETTING_DEFS)
    features["theme.editable"] = theme_mutable

    normalized = dict(value)
    normalized["format"] = PROFILE_FORMAT
    normalized["schemaVersion"] = version
    normalized["features"] = {**raw_features, **features}
    normalized["settings"] = settings
    normalized["theme"] = theme
    return normalized


def load_profile(path: Path) -> dict:
    try:
        profile_path = Path(path)
        if profile_path.stat().st_size > 32768:
            raise ValueError("Build profile is too large (maximum 32 KiB).")
        data = json.loads(profile_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Could not read build profile: {error}") from error
    return normalize_profile(data)


def compiler_flags(profile: dict) -> list[str]:
    features = profile["features"]
    flags = [f"-D{macro}={int(features[key])}"
             for key, macro in FEATURE_MACROS.items()]
    mutable_mask = 0
    for key, (setting_id, _, default_value) in SETTING_DEFS.items():
        setting = profile["settings"][key]
        if setting["mutable"]:
            mutable_mask |= 1 << setting_id
        flags.append(f"-DFW_DEFAULT_SETTING_{setting_id}={setting['default']}")
    if features["settings.mutable"]:
        mutable_mask |= (1 << 14) | (1 << 18) | (1 << 19)
    flags.append(f"-DFW_SETTING_MUTABLE_MASK=0x{mutable_mask:08X}UL")
    for index, color in enumerate(profile["theme"]["colors"]):
        flags.append(f"-DFW_THEME_COLOR_{index}=0x{rgb888_to_rgb565(color):04X}u")
    flags.append(f"-DFW_THEME_DIMMING={profile['theme']['dimming']}u")
    return flags


def rgb888_to_rgb565(color: str) -> int:
    value = int(color[1:], 16)
    red, green, blue = (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def rgb565_to_rgb888(color: int) -> int:
    red = ((color >> 11) & 0x1F) * 255 // 31
    green = ((color >> 5) & 0x3F) * 255 // 63
    blue = (color & 0x1F) * 255 // 31
    return (red << 16) | (green << 8) | blue


def write_profile(path: Path, profile: dict) -> None:
    Path(path).write_text(json.dumps(profile, indent=2, sort_keys=True) + "\n",
                          encoding="utf-8")
