"""Assemble the static GitHub Pages site for Dosimeter."""

from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
import html
import json
import os
from pathlib import Path
import re
import shutil
from urllib.request import Request, urlopen
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "dist/gh-pages"
DEFAULT_SPI_MINI = ROOT / ".pio/build/geiger2_spi_mini/firmware.bin"


def release_time(release: dict) -> datetime:
    """Compare publication times, including releases promoted from older tags."""
    published = release.get("published_at")
    if published:
        try:
            timestamp = datetime.fromisoformat(str(published).replace("Z", "+00:00"))
            return timestamp.replace(tzinfo=timezone.utc) if timestamp.tzinfo is None else timestamp
        except ValueError:
            pass
    # Legacy entries may omit published_at. Our tags carry a UTC build time.
    match = re.match(r"^(\d{12})(?:-|$)", str(release.get("tag_name", "")))
    if match:
        try:
            return datetime.strptime(match.group(1), "%Y%m%d%H%M").replace(tzinfo=timezone.utc)
        except ValueError:
            pass
    return datetime.min.replace(tzinfo=timezone.utc)


def prerelease_section(group: str, title: str, rows: list[str]) -> str:
    if not rows:
        return ""
    return (f'<section id="{group}-section" '
            f'aria-labelledby="{group}-heading">'
            f'<h2 id="{group}-heading">{html.escape(title)} ({len(rows)})</h2>'
            f'<ul class="release-list" id="{group}">{"".join(rows)}</ul></section>')


def stable_history(rows: list[str]) -> str:
    toggle = ('<input type="checkbox" class="release-history-toggle-input" '
              'id="releases-toggle" aria-controls="stable-releases" '
              'aria-label="Show all releases">' if len(rows) > 3 else "")
    label = ('<label class="release-history-toggle" for="releases-toggle">'
             '<span class="show-more">Show more</span>'
             '<span class="show-less">Show less</span></label>' if toggle else "")
    entries = "".join(rows) or '<li class="empty">No published app versions yet.</li>'
    return ('<section class="release-history" id="stable-history">'
            f'<h2>RELEASES ({len(rows)})</h2>{toggle}'
            f'<ul id="stable-releases">{entries}</ul>{label}</section>')


def prerelease_history(recent: list[str], older: list[str]) -> str:
    if not (recent or older):
        return ""
    groups = [("recent-prereleases", "PRE-RELEASES", recent),
              ("older-prereleases", "OLDER PRE-RELEASES", older)]
    controls = " ".join(group for group, _, rows in groups if rows)
    toggle = ('<input type="checkbox" class="release-history-toggle-input" '
              f'id="prereleases-toggle" aria-controls="{controls}" '
              'aria-label="Show all pre-releases">' if len(recent) > 1 or older else "")
    label = ('<label class="release-history-toggle" for="prereleases-toggle">'
             '<span class="show-more">Show more</span>'
             '<span class="show-less">Show less</span></label>' if toggle else "")
    sections = "".join(prerelease_section(group, title, rows) for group, title, rows in groups)
    return f'<div class="release-history">{toggle}{sections}{label}</div>'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT,
                        help="site output directory (default: dist/gh-pages)")
    parser.add_argument("--releases-json", type=Path,
                        help="GitHub releases API response used to add versioned app snapshots")
    parser.add_argument("--spi-mini-bin", type=Path, default=DEFAULT_SPI_MINI,
                        help="SPI service firmware embedded in the live app")
    args = parser.parse_args()
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.mkdir(parents=True, exist_ok=True)

    shutil.copytree(ROOT / "app", output / "app", dirs_exist_ok=True)
    mini_path = args.spi_mini_bin.resolve()
    if not mini_path.is_file():
        raise FileNotFoundError(f"SPI service firmware is missing: {mini_path}")
    mini_bytes = mini_path.read_bytes()
    if not mini_bytes or len(mini_bytes) >= 65536:
        raise ValueError(f"SPI service firmware has invalid size: {len(mini_bytes)} bytes")
    app_index = output / "app/index.html"
    app_document = app_index.read_text(encoding="utf-8")
    injection = ("<script id=\"embedded-spi-mini\">window.EMBEDDED_SPI_MINI_B64=\"" +
                 base64.b64encode(mini_bytes).decode("ascii") + "\";</script>")
    app_document, embedded_count = re.subn(
        r'<script\s+id="embedded-spi-mini">.*?</script>',
        lambda _: injection, app_document, count=1, flags=re.DOTALL,
    )
    if not embedded_count:
        if "</head>" not in app_document:
            raise ValueError("Could not find the app document head for the embedded SPI image")
        app_document = app_document.replace("</head>", injection + "</head>", 1)
    app_index.write_text(app_document, encoding="utf-8")
    shutil.copytree(ROOT / "docs/renders/screens",
                    output / "docs/renders/screens", dirs_exist_ok=True)
    template = (ROOT / "tools/pages-index.html").read_text(encoding="utf-8")
    stable_rows: list[str] = []
    recent_prerelease_rows: list[str] = []
    older_prerelease_rows: list[str] = []
    releases: list[dict] = []
    if args.releases_json:
        releases = json.loads(args.releases_json.read_text(encoding="utf-8"))
        if not isinstance(releases, list):
            raise ValueError("GitHub releases response must be a JSON array")
        if not releases:
            raise ValueError("GitHub returned no releases; refusing to publish an empty release portal")
    releases = [release for release in releases if not release.get("draft") and
                re.fullmatch(r"[a-zA-Z0-9._-]+", str(release.get("tag_name", "")))]
    if args.releases_json and not releases:
        raise ValueError("No published releases had valid tags; refusing to publish an empty release portal")
    releases.sort(key=release_time, reverse=True)
    latest_stable = next((release for release in releases if not release.get("prerelease")), None)
    stable_time = release_time(latest_stable) if latest_stable else None
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    archived_apps = 0
    for release in releases:
        tag = str(release.get("tag_name", ""))
        if not tag or any(char not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-" for char in tag):
            continue
        assets = {item.get("name"): item for item in release.get("assets", [])}
        html_asset = assets.get("Dosimeter.html") or next(
            (item for name, item in assets.items()
             if isinstance(name, str) and name.lower().endswith(".html")),
            None,
        )
        windows_asset = assets.get("Dosimeter.exe")
        legacy_windows_asset = assets.get("Dosimetr.exe")
        slug = quote(tag, safe="-_.")
        version_dir = output / "versions" / slug
        if html_asset:
            version_dir.mkdir(parents=True, exist_ok=True)
            request = Request(html_asset["url"], headers={
                "Accept": "application/octet-stream",
                "X-GitHub-Api-Version": "2022-11-28",
                **({"Authorization": f"Bearer {token}"} if token else {}),
            })
            with urlopen(request, timeout=60) as response:
                (version_dir / "index.html").write_bytes(response.read())
            shutil.copy2(ROOT / "app/app-icon.svg", version_dir / "app-icon.svg")
            manifest = {
                "id": "./index.html",
                "name": "Dosimeter",
                "short_name": "Dosimeter",
                "start_url": "./index.html",
                "scope": "./",
                "display": "standalone",
                "background_color": "#0b1115",
                "theme_color": "#0b1115",
                "icons": [{"src": "app-icon.svg", "sizes": "any",
                           "type": "image/svg+xml", "purpose": "any maskable"}],
            }
            (version_dir / "manifest.webmanifest").write_text(
                json.dumps(manifest, ensure_ascii=False, separators=(",", ":")),
                encoding="utf-8")
            worker = f'''const C="device-monitor-{slug}";self.addEventListener("install",e=>{{e.waitUntil(caches.open(C).then(c=>c.addAll(["./index.html","./manifest.webmanifest","./app-icon.svg"])));self.skipWaiting()}});self.addEventListener("activate",e=>e.waitUntil(self.clients.claim()));self.addEventListener("fetch",e=>{{if(e.request.method==="GET"&&new URL(e.request.url).origin===self.location.origin)e.respondWith(caches.match(e.request).then(r=>r||fetch(e.request)))}});'''
            (version_dir / "service-worker.js").write_text(worker, encoding="utf-8")
            if windows_asset is None and legacy_windows_asset:
                request = Request(legacy_windows_asset["url"], headers={
                    "Accept": "application/octet-stream",
                    "X-GitHub-Api-Version": "2022-11-28",
                    **({"Authorization": f"Bearer {token}"} if token else {}),
                })
                with urlopen(request, timeout=120) as response:
                    (version_dir / "Dosimeter.exe").write_bytes(response.read())
            archived_apps += 1
        title = html.escape(str(release.get("name") or tag))
        timestamp = re.match(r"^(\d{12})(?:-|$)", tag)
        if timestamp:
            version_time = timestamp.group(1)
            published = (f"{version_time[:4]}-{version_time[4:6]}-"
                         f"{version_time[6:8]} {version_time[8:10]}:"
                         f"{version_time[10:12]} UTC")
        else:
            published = str(release.get("published_at", ""))[:10]
        published = html.escape(published)
        url = f"./versions/{slug}/"
        notes_url = html.escape(str(release.get("html_url", "#")), quote=True)
        downloads = ([f'<a href="{url}">Open</a>'] if html_asset else [])
        if html_asset:
            html_download = html.escape(str(html_asset.get("browser_download_url", "")), quote=True)
            if html_download:
                downloads.append(f'<a href="{html_download}">HTML</a>')
        for asset_name, label in (("Dosimeter.exe", "Windows"),
                                  ("firmware.bin", "BIN"),
                                  ("spi-service-firmware.bin", "SPI service")):
            release_asset = windows_asset if asset_name == "Dosimeter.exe" else assets.get(asset_name)
            download_url = release_asset.get("browser_download_url") if release_asset else None
            if asset_name == "Dosimeter.exe" and not windows_asset and legacy_windows_asset:
                download_url = (f"{url}Dosimeter.exe" if html_asset else
                                legacy_windows_asset.get("browser_download_url"))
            if download_url:
                safe_download = html.escape(str(download_url), quote=True)
                downloads.append(f'<a href="{safe_download}">{label}</a>')
        downloads.append(f'<a href="{notes_url}">notes</a>')
        release_target = url if html_asset else notes_url
        row = (f'<li><div class="version"><a href="{release_target}">{title}</a>'
               f'<span class="meta"><span class="release-date">{published} · {html.escape(tag)}</span>'
               f'<span class="downloads">{" · ".join(downloads)}</span></span></div></li>')
        if not release.get("prerelease"):
            stable_rows.append(row)
        elif stable_time is None or release_time(release) > stable_time:
            recent_prerelease_rows.append(row)
        else:
            older_prerelease_rows.append(row)

    if releases and not (stable_rows or recent_prerelease_rows or older_prerelease_rows):
        raise ValueError("GitHub releases were returned, but none had valid tags; refusing to publish an empty release portal")

    template = template.replace("<!-- STABLE_RELEASES -->", stable_history(stable_rows))
    template = template.replace("<!-- PRERELEASE_HISTORY -->",
                                prerelease_history(recent_prerelease_rows, older_prerelease_rows))
    # Each archived app is standalone; give it a scoped manifest and an offline cache worker.
    for release in releases:
        tag = str(release.get("tag_name", ""))
        if not tag or any(char not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-" for char in tag):
            continue
        version_dir = output / "versions" / quote(tag, safe="-_.")
        if not (version_dir / "index.html").is_file():
            continue
        app_html = (version_dir / "index.html").read_text(encoding="utf-8")
        if 'href="manifest.webmanifest"' not in app_html:
            app_html = app_html.replace("<head>", '<head><link rel="manifest" href="manifest.webmanifest">', 1)
        if 'href="app-icon.svg"' not in app_html:
            app_html = app_html.replace("<head>", '<head><link rel="icon" href="app-icon.svg" type="image/svg+xml">', 1)
        (version_dir / "index.html").write_text(app_html, encoding="utf-8")
    (output / "index.html").write_text(template, encoding="utf-8")
    print(f"GitHub Pages site: {output} ({len(stable_rows)} stable, "
          f"{len(recent_prerelease_rows)} newer and {len(older_prerelease_rows)} previous "
          f"pre-releases, {archived_apps} app snapshots)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
