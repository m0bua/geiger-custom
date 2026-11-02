"""Verify that the Windows app's staged HTML embeds the expected firmware images."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re

from stock_firmware import STOCK_DIR, stock_hex_files


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


def read_stock_images(document: str) -> list[list[str]]:
    match = re.search(r"(?:window|globalThis)\.EMBEDDED_STOCK_FIRMWARES\s*=\s*", document)
    if match is None:
        raise ValueError("Embedded stock firmware list was not found.")
    end = document.find(";", match.end())
    if end < 0:
        raise ValueError("Embedded stock firmware list is incomplete.")
    statement = document[match.end():end]
    pairs = re.findall(
        r"\[\s*['\"]([^'\"]+)['\"]\s*,\s*['\"]([A-Za-z0-9+/=]+)['\"]\s*\]",
        statement,
    )
    return [[name, encoded] for name, encoded in pairs]


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
    stock_images = read_stock_images(document)
    stock_paths = stock_hex_files()
    if len(stock_images) != len(stock_paths):
        raise ValueError(f"Expected {len(stock_paths)} stock HEX images, found {len(stock_images)}.")
    for entry, path in zip(stock_images, stock_paths):
        if (not isinstance(entry, list) or len(entry) != 2 or
                entry[0] != path.name or not isinstance(entry[1], str)):
            raise ValueError(f"Unexpected embedded stock image entry for {path.name}.")
        embedded = base64.b64decode(entry[1], validate=True)
        expected = path.read_bytes()
        if embedded != expected:
            raise ValueError(f"Embedded stock HEX differs from {path}.")
        print(f"Verified stock HEX {path.name}: SHA-256 {hashlib.sha256(embedded).hexdigest()}")
    source_match = re.search(r"(?:window|globalThis)\.EMBEDDED_STOCK_SOURCE_LINKS\s*=\s*", document)
    if source_match is None:
        raise ValueError("Embedded stock firmware source links were not found.")
    try:
        embedded_sources, _ = json.JSONDecoder().raw_decode(document[source_match.end():])
    except json.JSONDecodeError as error:
        raise ValueError("Embedded stock firmware source links are incomplete.") from error
    source_manifest = json.loads((STOCK_DIR / "sources.json").read_text(encoding="utf-8"))
    if embedded_sources != source_manifest.get("links", []):
        raise ValueError("Embedded stock source links differ from firmware/stock/sources.json.")
    print(f"Verified {len(embedded_sources)} stock firmware source links.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
