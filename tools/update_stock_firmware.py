"""Fetch newly published Milerd Device HEX images into firmware/stock."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from urllib.parse import quote, unquote, urlparse, urlunparse
from urllib.request import Request, urlopen

from build_release import validate_stock_hex
from stock_firmware import STOCK_DIR


ALLOWED_HOSTS = {"milerdport.cloud", "mainserver.milerd.ru"}
PORT_API_ROOT = "https://milerdport.cloud/api/firmware_updates"
RU_API_ROOT = "https://mainserver.milerd.ru/api/firmware_updates"
MANIFEST = STOCK_DIR / "sources.json"
MAX_IMAGE_BYTES = 1024 * 1024
SOURCES = (
    ("current-E", f"{PORT_API_ROOT}/current?platform=Device&type=E"),
    ("last-E", f"{PORT_API_ROOT}/getLast?platform=Device&type=E"),
    ("last-T", f"{PORT_API_ROOT}/getLast?platform=Device&type=T"),
    ("current-default", f"{PORT_API_ROOT}/current?platform=Device"),
    ("current-RU", f"{RU_API_ROOT}/current?platform=Device&type=u"),
    ("last-RU", f"{RU_API_ROOT}/getLast?platform=Device&type=u"),
)
VERSION_RE = re.compile(r"(?<!\d)(\d+)\.(\d+)\.(\d+)(?!\d)")


def fetch(url: str, *, json_response: bool = False) -> bytes | object:
    request = Request(url, headers={"User-Agent": "Dosimeter-stock-firmware-updater/1.0"})
    with urlopen(request, timeout=25) as response:
        final = urlparse(response.geturl())
        if final.scheme != "https" or final.hostname not in ALLOWED_HOSTS:
            raise ValueError(f"Refusing unexpected firmware source redirect: {response.geturl()}")
        data = response.read(MAX_IMAGE_BYTES + 1)
    if len(data) > MAX_IMAGE_BYTES:
        raise ValueError(f"Response exceeds {MAX_IMAGE_BYTES} bytes: {url}")
    if json_response:
        return json.loads(data.decode("utf-8"))
    return data


def secure_url(url: str) -> str:
    parsed = urlparse(url)
    if parsed.hostname not in ALLOWED_HOSTS or parsed.scheme not in ("http", "https"):
        raise ValueError(f"Unexpected firmware download URL: {url}")
    return urlunparse(("https", parsed.netloc, parsed.path, parsed.params, parsed.query, ""))


def api_candidates() -> list[tuple[str, str, str | None, str]]:
    candidates: list[tuple[str, str, str | None, str]] = []
    for source_name, endpoint in SOURCES:
        payload = fetch(endpoint, json_response=True)
        if not isinstance(payload, dict):
            raise ValueError(f"Expected a JSON object from {endpoint}")

        if source_name.startswith("current-"):
            relative = payload.get("file_path")
            if not isinstance(relative, str) or not relative:
                raise ValueError(f"Missing file_path in response from {endpoint}")
            host = urlparse(endpoint).hostname
            file_url = f"https://{host}/uploads/{quote(relative.lstrip('/'), safe='/()-_') }"
            candidates.append((source_name, file_url, str(payload.get("version") or "") or None, endpoint))
            continue

        files = payload.get("files")
        if not isinstance(files, list) or not files:
            raise ValueError(f"Missing files list in response from {endpoint}")
        for file_url in files:
            if not isinstance(file_url, str):
                raise ValueError(f"Invalid file URL in response from {endpoint}")
            candidates.append((source_name, secure_url(file_url), None, endpoint))
    return candidates


def safe_image_name(url: str) -> str:
    name = unquote(Path(urlparse(url).path).name)
    if not name.lower().endswith(".hex") or name in ("", ".", ".."):
        raise ValueError(f"Expected a .hex filename in {url}")
    if any(char not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._()- " for char in name):
        raise ValueError(f"Unexpected characters in firmware filename: {name!r}")
    return name


def load_manifest() -> dict:
    if not MANIFEST.exists():
        return {"format": 1, "files": {}}
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if data.get("format") != 1 or not isinstance(data.get("files"), dict):
        raise ValueError(f"Unsupported source manifest format: {MANIFEST}")
    return data


def unique_destination(filename: str, digest: str) -> Path:
    path = STOCK_DIR / filename
    if not path.exists():
        return path
    suffix = f"-{digest[:8]}"
    return path.with_name(f"{path.stem}{suffix}{path.suffix}")


def main() -> int:
    STOCK_DIR.mkdir(parents=True, exist_ok=True)
    manifest = load_manifest()
    known_hashes: dict[str, str] = {}
    for path in STOCK_DIR.glob("*.hex"):
        known_hashes[hashlib.sha256(path.read_bytes()).hexdigest()] = path.name

    changed = False
    checked: set[tuple[str, str]] = set()
    for source, file_url, metadata_version, api_url in api_candidates():
        file_url = secure_url(file_url)
        key = (source, file_url)
        if key in checked:
            continue
        checked.add(key)
        filename = safe_image_name(file_url)
        raw = fetch(file_url)
        assert isinstance(raw, bytes)
        validate_stock_hex(Path(filename), raw)
        digest = hashlib.sha256(raw).hexdigest()
        version_match = VERSION_RE.search(filename) or VERSION_RE.search(metadata_version or "")
        version = ".".join(version_match.groups()) if version_match else metadata_version

        existing_name = known_hashes.get(digest)
        if existing_name is None:
            destination = unique_destination(filename, digest)
            destination.write_bytes(raw)
            existing_name = destination.name
            known_hashes[digest] = existing_name
            changed = True
            print(f"Added {existing_name} · {version or 'version unknown'} · SHA-256 {digest}")
        else:
            print(f"Already present: {existing_name} · {version or 'version unknown'}")

        record = manifest["files"].get(existing_name)
        if not isinstance(record, dict):
            record = {"sha256": digest, "version": version, "source_urls": []}
            manifest["files"][existing_name] = record
            changed = True
        elif record.get("sha256") != digest:
            raise ValueError(f"Manifest hash mismatch for {existing_name}")
        sources = record.setdefault("source_urls", [])
        source_record = {"api": api_url, "download": file_url}
        if source_record not in sources:
            sources.append(source_record)
            changed = True

    if changed:
        MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Updated provenance manifest: {MANIFEST.relative_to(STOCK_DIR.parents[1])}")
    else:
        print("No new firmware images or source records.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
