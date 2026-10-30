"""Build the single-file Windows desktop app from the shared web app sources."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
APP = ROOT / "app"
SCRATCH = ROOT / ".pio/scratch/windows-app"
STAGE = SCRATCH / "bundle"
FIRMWARE = ROOT / ".pio/scratch/geiger2_manual/firmware.bin"


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def clear_build_dir(path: Path) -> None:
    resolved = path.resolve()
    scratch_root = (ROOT / ".pio/scratch").resolve()
    if resolved == scratch_root or scratch_root not in resolved.parents:
        raise RuntimeError(f"Refusing to clear a path outside the Windows app build area: {resolved}")
    if resolved.exists():
        shutil.rmtree(resolved)


def configure_windows_html(path: Path) -> None:
    document = path.read_text(encoding="utf-8")
    document, usb_count = re.subn(
        r'<button id="connect-usb-button"[^>]*>Connect USB</button>',
        "", document, count=1,
    )
    if usb_count != 1:
        raise RuntimeError("Could not remove the browser-only Connect USB button from EXE UI.")
    document, connect_count = re.subn(
        r'(<button id="connect-button"[^>]*>)',
        '<select id="windows-bridge-port" class="windows-bridge-port" '
        'aria-label="Windows serial port" hidden></select>\\1',
        document, count=1,
    )
    if connect_count != 1:
        raise RuntimeError("Could not add the Windows COM port selector to EXE UI.")
    document = document.replace(
        "Desktop: connect over Web Serial. Android: use Connect USB for a CP210x adapter. "
        "Opening a serial port may restart the device.",
        "Windows app: connect through the selected COM port. Firmware programming uses the same connection.",
    )
    path.write_text(document, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="version shown in About; defaults to build time plus -local")
    parser.add_argument("--toolchain", type=Path,
                        help="ARM GCC bin directory passed to firmware builder")
    parser.add_argument("--firmware-bin", type=Path,
                        help="reuse a prebuilt release BIN without recompiling it")
    parser.add_argument("--spi-mini-bin", type=Path,
                        help="reuse a prebuilt SPI service BIN")
    parser.add_argument("--require-minifier", action="store_true",
                        help="require Node.js/Terser when generating the embedded HTML")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist/windows",
                        help="directory for the resulting executable")
    args = parser.parse_args()

    if sys.platform != "win32":
        raise SystemExit("Build the Windows executable on Windows; PyInstaller is not a cross-compiler.")
    try:
        import PyInstaller  # noqa: F401
        import serial  # noqa: F401
        import webview  # noqa: F401
    except ImportError as error:
        raise SystemExit(
            "Missing desktop build dependencies. Run: "
            "python -m pip install -r tools/windows_app_requirements.txt"
        ) from error

    clear_build_dir(SCRATCH)
    app_bundle = STAGE / "app"
    app_bundle.mkdir(parents=True)
    for asset in APP.iterdir():
        if asset.name == "index.html":
            continue
        target = app_bundle / asset.name
        if asset.is_dir():
            shutil.copytree(asset, target)
        else:
            shutil.copy2(asset, target)

    embedded_html = app_bundle / "index.html"
    command = [sys.executable, str(TOOLS / "build_release.py"),
               "--output", str(embedded_html)]
    if args.version:
        command.extend(["--version", args.version])
    if args.toolchain:
        command.extend(["--toolchain", str(args.toolchain.resolve())])
    if args.firmware_bin:
        command.extend(["--firmware-bin", str(args.firmware_bin.resolve())])
    if args.spi_mini_bin:
        command.extend(["--spi-mini-bin", str(args.spi_mini_bin.resolve())])
    if args.require_minifier:
        command.append("--require-minifier")
    run(command)
    configure_windows_html(embedded_html)
    firmware_source = args.firmware_bin.resolve() if args.firmware_bin else FIRMWARE
    if not firmware_source.is_file():
        raise FileNotFoundError(firmware_source)
    if firmware_source != FIRMWARE.resolve():
        FIRMWARE.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(firmware_source, FIRMWARE)

    icon_path = APP / "app-icon.ico"
    run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
         str(TOOLS / "build_windows_icon.ps1"), "-OutputPath", str(icon_path)])

    firmware_alias = STAGE / ".pio/build/firmware.bin"
    firmware_alias.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(firmware_source, firmware_alias)

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    work_dir = SCRATCH / "pyinstaller-work"
    spec_dir = SCRATCH / "spec"
    command = [
        sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile",
        "--windowed", "--name", "Dosimetr", "--distpath", str(output_dir),
        "--workpath", str(work_dir), "--specpath", str(spec_dir),
        "--icon", str(icon_path),
        "--add-data", f"{STAGE};.", "--collect-all", "webview",
        "--collect-all", "clr_loader", "--collect-all", "pythonnet",
        "--collect-all", "serial", str(TOOLS / "geiger2_desktop.py"),
    ]
    run(command)
    executable = output_dir / "Dosimetr.exe"
    print(f"Windows app: {executable} ({executable.stat().st_size:,} bytes)")
    print("The executable contains the shared UI, firmware image, and local COM bridge.")
    print("Microsoft Edge WebView2 Runtime is required on the target PC.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
