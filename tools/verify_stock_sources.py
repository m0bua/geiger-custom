"""Validate stock firmware files and their recorded source metadata offline."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from urllib.parse import urlparse

from build_release import validate_stock_hex
from stock_firmware import STOCK_DIR, stock_hex_files


def require_https(value: object, context: str) -> None:
    if not isinstance(value, str):
        raise ValueError(f"{context} must be a URL string.")
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ValueError(f"{context} must use an absolute HTTPS URL: {value!r}")


def main() -> int:
    manifest_path = STOCK_DIR / "sources.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != 1:
        raise ValueError(f"Unsupported stock source manifest format: {manifest_path}")

    endpoints = manifest.get("api_endpoints")
    links = manifest.get("links")
    files = manifest.get("files")
    if not isinstance(endpoints, list) or not isinstance(links, list) or not isinstance(files, dict):
        raise ValueError(f"Stock source manifest has invalid fields: {manifest_path}")
    for index, endpoint in enumerate(endpoints):
        require_https(endpoint, f"api_endpoints[{index}]")
    for index, entry in enumerate(links):
        if not isinstance(entry, dict) or not all(
            isinstance(entry.get(key), str) and entry[key].strip()
            for key in ("label", "description", "url")
        ):
            raise ValueError(f"Invalid source link at links[{index}].")
        require_https(entry["url"], f"links[{index}].url")

    stock_paths = stock_hex_files()
    if not stock_paths:
        raise FileNotFoundError(f"No stock HEX files found in {STOCK_DIR}.")
    for path in stock_paths:
        raw = path.read_bytes()
        validate_stock_hex(path, raw)
        record = files.get(path.name)
        if record is None:
            print(f"Validated stock HEX without provenance record: {path.name}")
            continue
        if not isinstance(record, dict):
            raise ValueError(f"Invalid provenance record for {path.name}.")
        digest = hashlib.sha256(raw).hexdigest()
        if record.get("sha256") != digest:
            raise ValueError(f"Stock HEX hash differs from manifest: {path.name}")
        source_urls = record.get("source_urls")
        if not isinstance(source_urls, list):
            raise ValueError(f"Invalid source_urls for {path.name}.")
        for index, source in enumerate(source_urls):
            if not isinstance(source, dict):
                raise ValueError(f"Invalid source_urls[{index}] for {path.name}.")
            require_https(source.get("api"), f"{path.name} source_urls[{index}].api")
            require_https(source.get("download"), f"{path.name} source_urls[{index}].download")
        print(f"Validated {path.name} · SHA-256 {digest}")

    print(f"Validated {len(links)} source links and {len(stock_paths)} stock HEX images.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
