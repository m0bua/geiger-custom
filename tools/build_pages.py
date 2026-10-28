"""Assemble the static GitHub Pages site for the device monitor."""

from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path
import shutil
from urllib.request import Request, urlopen
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "dist/gh-pages"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT,
                        help="site output directory (default: dist/gh-pages)")
    parser.add_argument("--releases-json", type=Path,
                        help="GitHub releases API response used to add versioned app snapshots")
    args = parser.parse_args()
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.mkdir(parents=True, exist_ok=True)

    shutil.copytree(ROOT / "app", output / "app", dirs_exist_ok=True)
    shutil.copytree(ROOT / "docs/renders/screens",
                    output / "docs/renders/screens", dirs_exist_ok=True)
    template = (ROOT / "tools/pages-index.html").read_text(encoding="utf-8")
    stable_rows: list[str] = []
    prerelease_rows: list[str] = []
    releases: list[dict] = []
    if args.releases_json:
        releases = json.loads(args.releases_json.read_text(encoding="utf-8"))
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    for release in releases:
        tag = str(release.get("tag_name", ""))
        if not tag or any(char not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-" for char in tag):
            continue
        asset = next((item for item in release.get("assets", [])
                      if item.get("name") == "Dosimeter-App.html"), None)
        if not asset:
            continue
        slug = quote(tag, safe="-_.")
        version_dir = output / "versions" / slug
        version_dir.mkdir(parents=True, exist_ok=True)
        request = Request(asset["url"], headers={
            "Accept": "application/octet-stream",
            "X-GitHub-Api-Version": "2022-11-28",
            **({"Authorization": f"Bearer {token}"} if token else {}),
        })
        with urlopen(request, timeout=60) as response:
            (version_dir / "index.html").write_bytes(response.read())
        shutil.copy2(ROOT / "app/app-icon.svg", version_dir / "app-icon.svg")
        manifest = {
            "id": "./index.html",
            "name": release.get("name") or f"Device Monitor {tag}",
            "short_name": f"Monitor {tag}",
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
        title = html.escape(str(release.get("name") or tag))
        published = html.escape(str(release.get("published_at", ""))[:10])
        url = f"./versions/{slug}/"
        notes_url = html.escape(str(release.get("html_url", "#")), quote=True)
        row = (f'<li><div class="version"><a href="{url}">{title}</a>'
               f'<span class="meta">{published} · {html.escape(tag)} · '
               f'<a href="{notes_url}">notes</a></span></div></li>')
        (prerelease_rows if release.get("prerelease") else stable_rows).append(row)

    empty = '<li class="empty">No published app versions yet.</li>'
    template = template.replace("<!-- STABLE_RELEASES -->", "".join(stable_rows) or empty)
    template = template.replace("<!-- PRERELEASES -->", "".join(prerelease_rows) or empty)
    template = template.replace('id="prerelease-count">0</span>',
                                f'id="prerelease-count">{len(prerelease_rows)}</span>')
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
    print(f"GitHub Pages site: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
