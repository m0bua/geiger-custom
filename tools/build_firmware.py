"""Build the bare-metal firmware using the installed ARM toolchain.

This is also usable when PlatformIO cannot open its global package lock.
Run: python tools/build_firmware.py [--test-support] [--toolchain BIN_DIR]
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = Path.home() / '.platformio/packages'
# Match the GCC 7.2.1 package selected by the current PlatformIO environment.
# The unversioned installation may contain a different compiler and yield a
# misleading Flash margin near the 64-KiB limit.
TOOLCHAIN = PACKAGES / 'toolchain-gccarmnoneeabi@1.70201.0/bin'
if not TOOLCHAIN.is_dir():
    TOOLCHAIN = PACKAGES / 'toolchain-gccarmnoneeabi/bin'
if os.environ.get('ARM_GCC_BIN'):
    TOOLCHAIN = Path(os.environ['ARM_GCC_BIN']).expanduser()


def executable(toolchain, name):
    candidates = [toolchain / f'{name}.exe', toolchain / name]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    located = shutil.which(name)
    if located:
        return located
    raise FileNotFoundError(
        f'Could not find {name} in {toolchain} or PATH. '
        'Install the GNU Arm Embedded toolchain or set ARM_GCC_BIN.'
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--test-support', action='store_true')
    parser.add_argument('--toolchain', type=Path, default=TOOLCHAIN)
    args = parser.parse_args()
    out = ROOT / '.pio/scratch' / ('geiger2_test' if args.test_support else 'geiger2_manual')
    out.mkdir(parents=True, exist_ok=True)
    toolchain = args.toolchain
    gcc = executable(toolchain, 'arm-none-eabi-gcc')
    print(f'Toolchain: {toolchain}', flush=True)
    flags = ['-std=gnu11', '-mcpu=cortex-m0plus', '-mthumb', '-Os', '-g2',
             '-ffunction-sections', '-fdata-sections', '-fstack-usage', '-Wall', '-Wextra', '-Werror',
             '-fno-inline-small-functions', '-fno-tree-switch-conversion',
             '-I', str(ROOT / 'include')]
    sources = sorted(source for source in (ROOT / 'src').glob('*.c')
                     if not source.name.startswith('spi_mini_'))
    if args.test_support:
        sources.append(ROOT / 'tests/test_log_engine_support.c')
    objects = []
    for source in sources:
        obj = out / (source.stem + '.o')
        subprocess.run([gcc, *flags, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    startup = out / 'startup.o'
    subprocess.run([gcc, '-mcpu=cortex-m0plus', '-mthumb', '-c', str(ROOT / 'startup_stm32l051.S'), '-o', str(startup)], check=True)
    elf = out / 'firmware.elf'
    subprocess.run([gcc, '-mcpu=cortex-m0plus', '-mthumb', '-nostartfiles',
                    '-specs=nano.specs', '-specs=nosys.specs', '-Wl,--gc-sections',
                    f'-Wl,-Map,{out / "firmware.map"}',
                    *(['-Wl,--undefined=test_feed_seconds', '-Wl,--undefined=second_log_get'] if args.test_support else []),
                    '-T', str(ROOT / 'linker/STM32L051C8TX_FLASH.ld'),
                    *objects, str(startup), '-o', str(elf)], check=True)
    subprocess.run([executable(toolchain, 'arm-none-eabi-objcopy'), '-O', 'binary', str(elf), str(out / 'firmware.bin')], check=True)
    subprocess.run([executable(toolchain, 'arm-none-eabi-size'), str(elf)], check=True)
    image_size = (out / 'firmware.bin').stat().st_size
    print(f'Flash image: {image_size} / 65536 bytes; {65536 - image_size} bytes free')
    print(out)


if __name__ == '__main__':
    main()
