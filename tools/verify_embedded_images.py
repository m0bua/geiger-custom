"""Verify that the Windows app's staged HTML embeds the expected firmware images."""

from __future__ import annotations

import argparse
import base64
import hashlib
from pathlib import Path
import re


def read_embedded_image(document: str, name: str) -> bytes:
    pattern = re.compile(
        rf"(?:window|globalThis)\.{re.escape(name)}\s*=\s*\"([A-Za-z0-9+/=]+)\""
    )
    match = pattern.search(document)
    if match is None:
        raise ValueError(f"Embedded image assignment not found: {name}")
    return base64.b64decode(match.group(1), validate=True)


def verify(document: str, variable: str, expected_path: Path) -> str:
    expected = expected_path.read_bytes()
    embedded = read_embedded_image(document, variable)
    expected_hash = hashlib.sha256(expected).hexdigest()
    embedded_hash = hashlib.sha256(embedded).hexdigest()
    if embedded != expected:
        raise ValueError(
            f"{variable} differs from {expected_path}: "
            f"{embedded_hash} != {expected_hash}"
        )
    return embedded_hash


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--html", required=True, type=Path,
                        help="staged HTML included in the Windows app")
    parser.add_argument("--firmware-bin", required=True, type=Path)
    parser.add_argument("--spi-mini-bin", required=True, type=Path)
    args = parser.parse_args()

    document = args.html.read_text(encoding="utf-8")
    for variable, path in (
        ("EMBEDDED_FIRMWARE_B64", args.firmware_bin),
        ("EMBEDDED_SPI_MINI_B64", args.spi_mini_bin),
    ):
        digest = verify(document, variable, path)
        print(f"Verified {variable}: SHA-256 {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
