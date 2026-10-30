"""Build settings shared by PlatformIO and the standalone compiler driver."""

import configparser
import shlex
from pathlib import Path


RUNTIME_LINK_FLAGS = ["-nostartfiles", "-specs=nano.specs", "-specs=nosys.specs"]


def project_build_flags(root):
    """Use the main firmware profile, including optional local overrides."""
    root = Path(root)
    config = configparser.ConfigParser(interpolation=None)
    config.read(root / "platformio.ini", encoding="utf-8")
    for pattern in shlex.split(config.get("platformio", "extra_configs", fallback="")):
        for path in sorted(root.glob(pattern)):
            config.read(path, encoding="utf-8")
    flags = shlex.split(config.get("env:geiger2", "build_flags"))
    if any("${" in flag for flag in flags):
        raise ValueError("Unresolved PlatformIO build_flags interpolation")
    return flags
