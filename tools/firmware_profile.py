"""Validation and compiler flags for Quick Firmware build profiles."""

from __future__ import annotations

import json
from pathlib import Path


PROFILE_FORMAT = "geiger-custom-firmware-profile"
PROFILE_SCHEMA_VERSION = 1

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

    normalized = dict(value)
    normalized["format"] = PROFILE_FORMAT
    normalized["schemaVersion"] = version
    normalized["features"] = {**raw_features, **features}
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
    return [f"-D{macro}={int(features[key])}"
            for key, macro in FEATURE_MACROS.items()]


def write_profile(path: Path, profile: dict) -> None:
    Path(path).write_text(json.dumps(profile, indent=2, sort_keys=True) + "\n",
                          encoding="utf-8")
