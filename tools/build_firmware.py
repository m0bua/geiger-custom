"""Build the bare-metal firmware using the installed ARM toolchain.

This is also usable when PlatformIO cannot open its global package lock.
Run: python tools/build_firmware.py [--test-support] [--toolchain BIN_DIR]
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

from firmware_build_config import RUNTIME_LINK_FLAGS, project_build_flags
from check_firmware import check_firmware

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
    # A failed build must not leave an older image looking ready to flash.
    for suffix in ('.elf', '.bin', '.hex'):
        (out / ('firmware' + suffix)).unlink(missing_ok=True)
    toolchain = args.toolchain
    gcc = executable(toolchain, 'arm-none-eabi-gcc')
    print(f'Toolchain: {toolchain}', flush=True)
    shared_flags = project_build_flags(ROOT)
    flags = [flag for flag in shared_flags if not flag.startswith('-Wl,')]
    flags += [
             '-DF_CPU=16000000L', '-DPLATFORMIO=60200', '-DSTM32L051xx',
             '-I', str(ROOT / 'include'), '-I', str(ROOT)]
    if args.test_support:
        flags += ['-fstack-usage', '-fno-inline']
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
    platform_ldscripts = PACKAGES.parent / 'platforms/ststm32/ldscripts'
    link_flags = [flag.replace('$BUILD_DIR', str(out)) for flag in shared_flags
                  if flag.startswith(('-Wl,', '-O', '-mcpu=', '-mthumb', '-flto'))]
    linker_script = ROOT / 'linker/STM32L051C8TX_FLASH.ld'
    test_flash_size = 72 * 1024
    if args.test_support:
        linker_script = out / 'test_flash.ld'
        linker_script.write_text(
            (ROOT / 'linker/STM32L051C8TX_FLASH.ld').read_text(
                encoding='utf-8').replace('LENGTH = 64K', 'LENGTH = 72K'),
            encoding='utf-8')
    test_symbols = [
        'test_feed_seconds', 'second_log_get', 'rate_display_step',
        'rate_display_interval', 'rate_display_ticks', 'rate_display_update',
        'log_record_encode',
        'log_record_decode', 'log_record_time', 'log_record_location',
        'log_eeprom_init', 'log_eeprom_append', 'log_eeprom_read',
        'log_eeprom_count', 'log_eeprom_active_epoch',
        'log_eeprom_segment_year', 'log_history_begin', 'log_history_next',
        'log_writer_init', 'log_writer_second', 'log_writer_live',
        'log_writer_change_memory', 'log_writer_memory_committed',
        'spi_log_prepare', 'spi_log_append', 'spi_log_count',
        'spi_log_find_oldest', 'spi_log_erase_oldest',
        'spi_log_query_read', 'daily_dose_init', 'daily_dose_add',
        'daily_dose_count', 'daily_dose_total', 'daily_dose_set_day',
        'daily_dose_set_total', 'daily_dose_checkpoint', 'lcd_glyph_put',
        'lcd_text_stream_next', 'lcd_spi_pump', 'lcd_graph_prepare_glyph',
        'protocol_v1_service', 'board_display_log_memory',
        'board_display_log_source', 'board_display_rate_led_mask',
        'board_eeprom_read', 'board_eeprom_write', 'board_rtc_log_time',
        'spi_nor_is_ready', 'spi_nor_size_bytes', 'spi_nor_init',
        'spi_nor_read', 'spi_nor_program_page', 'spi_nor_erase_sector',
    ] if args.test_support else []
    subprocess.run([gcc, *RUNTIME_LINK_FLAGS, '-Wl,--gc-sections,--relax',
                    *link_flags,
                    *[f'-Wl,--undefined={symbol}' for symbol in test_symbols],
                    '-T', str(linker_script),
                    *objects, str(startup), '-L' + str(platform_ldscripts),
                    '-L' + str(out), '-Wl,--start-group', '-lc', '-lgcc', '-lm',
                    '-lstdc++', '-Wl,--end-group', '-o', str(elf)], check=True)
    check_firmware(elf, test_flash_size if args.test_support else 65536)
    subprocess.run([executable(toolchain, 'arm-none-eabi-objcopy'), '-O', 'binary', str(elf), str(out / 'firmware.bin')], check=True)
    subprocess.run([executable(toolchain, 'arm-none-eabi-objcopy'), '-O', 'ihex', str(elf), str(out / 'firmware.hex')], check=True)
    subprocess.run([executable(toolchain, 'arm-none-eabi-size'), str(elf)], check=True)
    image_size = (out / 'firmware.bin').stat().st_size
    flash_limit = test_flash_size if args.test_support else 65536
    print(f'Flash image: {image_size} / {flash_limit} bytes; {flash_limit - image_size} bytes free')
    if args.test_support:
        print('Test-support image exceeds the physical 64-KiB Flash and must not be flashed.')
    print(out)


if __name__ == '__main__':
    main()
