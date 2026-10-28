"""Build a standalone, single-file dosimeter app with its matching firmware."""

from __future__ import annotations

import argparse
import base64
import html
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "app"
DEFAULT_FIRMWARE = ROOT / ".pio/scratch/geiger2_manual/firmware.bin"
DEFAULT_OUTPUT = ROOT / "dist/Dosimeter-App.html"
TERSER_VERSION = "5.44.0"


def minify_javascript(source: str, required: bool) -> tuple[str, bool]:
    terser = os.environ.get("TERSER_BIN") or shutil.which("terser")
    command: list[str]
    if terser:
        command = [terser]
    else:
        npx = shutil.which("npx")
        if not npx:
            if required:
                raise RuntimeError("Terser is required but not found; install Node.js and Terser, or set TERSER_BIN.")
            return source, False
        command = [npx, "--yes", f"terser@{TERSER_VERSION}"]

    work = ROOT / ".pio/scratch/release-work"
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=work) as temp_dir:
        source_path = Path(temp_dir) / "app.js"
        output_path = Path(temp_dir) / "app.min.js"
        source_path.write_text(source, encoding="utf-8")
        command.extend([
            str(source_path), "--output", str(output_path), "--ecma", "2020",
            "--compress", "--mangle", "--toplevel",
            "--format", "comments=false",
        ])
        if os.name == "nt" and command[0].lower().endswith((".cmd", ".bat")):
            command.insert(0, "cmd")
            command.insert(1, "/c")
        subprocess.run(command, cwd=ROOT, check=True)
        return output_path.read_text(encoding="utf-8"), True


def minify_css(source: str) -> str:
    out: list[str] = []
    quote = ""
    escaped = False
    pending_space = False
    i = 0
    while i < len(source):
        char = source[i]
        if quote:
            out.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            i += 1
            continue
        if char in "\"'":
            if pending_space and out and out[-1] not in "{}:;,":
                out.append(" ")
            pending_space = False
            quote = char
            out.append(char)
            i += 1
            continue
        if source.startswith("/*", i):
            end = source.find("*/", i + 2)
            i = len(source) if end < 0 else end + 2
            pending_space = True
            continue
        if char.isspace():
            pending_space = True
            i += 1
            continue
        if pending_space and out and out[-1] not in "{}:;," and char not in "{}:;,":
            out.append(" ")
        pending_space = False
        out.append(char)
        i += 1
    return "".join(out).strip()


def minify_html(source: str) -> str:
    raw_pattern = re.compile(r"(?is)<(script|style|pre|textarea)\b[^>]*>.*?</\1\s*>")
    result: list[str] = []
    cursor = 0
    for match in raw_pattern.finditer(source):
        outside = source[cursor:match.start()]
        outside = re.sub(r"<!--[\s\S]*?-->", "", outside)
        outside = re.sub(r">\s+<", "> <", outside)
        result.append(outside.strip())
        result.append(match.group(0))
        cursor = match.end()
    outside = re.sub(r"<!--[\s\S]*?-->", "", source[cursor:])
    result.append(re.sub(r">\s+<", "> <", outside).strip())
    return "".join(result)


def inline_manual_images(document: str) -> str:
    pattern = re.compile(r'src="\.\./docs/renders/screens/([^"]+\.svg)"')

    def replace(match: re.Match[str]) -> str:
        image_path = ROOT / "docs/renders/screens" / match.group(1)
        if not image_path.is_file():
            raise FileNotFoundError(f"Manual illustration is missing: {image_path}")
        encoded = base64.b64encode(image_path.read_bytes()).decode("ascii")
        return f'src="data:image/svg+xml;base64,{encoded}"'

    return pattern.sub(replace, document)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT,
                        help=f"output HTML path (default: {DEFAULT_OUTPUT.relative_to(ROOT)})")
    parser.add_argument("--toolchain", type=Path,
                        help="optional ARM GCC bin directory passed to build_firmware.py")
    parser.add_argument("--version", default="local",
                        help="version label embedded in About (release tag, e.g. YYYYMMDDhhmm)")
    parser.add_argument("--require-minifier", action="store_true",
                        default=os.environ.get("CI", "").lower() == "true",
                        help="fail unless Terser is available (automatically required on CI)")
    args = parser.parse_args()

    build_command = [sys.executable, str(ROOT / "tools/build_firmware.py")]
    if args.toolchain:
        build_command.extend(["--toolchain", str(args.toolchain.resolve())])
    subprocess.run(build_command, cwd=ROOT, check=True)

    if not DEFAULT_FIRMWARE.is_file():
        raise FileNotFoundError(f"Firmware build did not produce {DEFAULT_FIRMWARE}")
    firmware = DEFAULT_FIRMWARE.read_bytes()
    if not firmware or len(firmware) > 65536:
        raise ValueError(f"Firmware image has invalid size: {len(firmware)} bytes")

    document = (APP / "index.html").read_text(encoding="utf-8")
    document, version_count = re.subn(
        r'(<strong id="app-version">).*?(</strong>)',
        lambda match: match.group(1) + html.escape(args.version) + match.group(2),
        document, count=1,
    )
    if version_count != 1:
        raise ValueError("Could not set the app version in About")
    css = minify_css((APP / "style.css").read_text(encoding="utf-8"))
    js = (APP / "app.js").read_text(encoding="utf-8")
    # The packaged app flashes its embedded image; keep the developer-only
    # local-build probe generic so the release contains no device branding.
    js = js.replace("../.pio/build/geiger2/firmware.bin", "../.pio/build/firmware.bin")

    document = re.sub(
        r'<link\s+rel="stylesheet"\s+href="style\.css[^"]*"\s*/?>',
        lambda _: f"<style>\n{css}\n</style>", document, count=1,
    )
    document = inline_manual_images(document)

    app_script = (
        "window.EMBEDDED_FIRMWARE_B64 = \""
        + base64.b64encode(firmware).decode("ascii")
        + "\";\n"
        + js
    )
    app_script, js_minified = minify_javascript(app_script, args.require_minifier)
    document = re.sub(
        r'<script\s+defer\s+src="app\.js[^"]*"\s*>\s*</script>',
        lambda _: f"<script>\n{app_script}\n</script>", document, count=1,
    )
    if "src=\"app.js" in document or "href=\"style.css" in document:
        raise ValueError("Could not inline the app's CSS and JavaScript")
    if "../docs/renders/screens/" in document:
        raise ValueError("At least one manual image was not embedded")

    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    document = minify_html(document)
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(document)
    print(f"Standalone app: {output}")
    print(f"Embedded firmware: {len(firmware)} / 65536 bytes")
    print(f"JavaScript minification: {'Terser with identifier mangling' if js_minified else 'unavailable; whitespace-safe HTML/CSS compaction only'}")
    print(f"Single-file package: {output.stat().st_size:,} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
