"""Discover stock HEX files and apply the catalog's stable version order."""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
STOCK_DIR = ROOT / "firmware/stock"
VERSION_RE = re.compile(r"(?<!\d)(\d+)\.(\d+)\.(\d+)(?!\d)")


def stock_hex_files() -> list[Path]:
    """Return every validly named HEX in firmware/stock, newest first.

    Equal versions prefer EN, then filenames sort alphabetically.
    Files without a semantic x.y.z version sort after versioned files.
    """
    files = list(STOCK_DIR.glob("*.hex"))

    def order(path: Path) -> tuple[object, ...]:
        match = VERSION_RE.search(path.stem)
        version = tuple(int(part) for part in match.groups()) if match else (0, 0, 0)
        language_rank = 0 if re.search(r"(?:^|[-_])EN(?:[-_.]|$)", path.stem, re.I) else 1
        return (-version[0], -version[1], -version[2], language_rank, path.name.casefold())

    return sorted(files, key=order)
