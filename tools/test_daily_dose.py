"""Run the dose reconciliation/checkpoint regression on the real ARM code.

Requires Unicorn in .pio/testdeps (same dependency as test_buttons.py).
Run: python tools/test_daily_dose.py
"""
from pathlib import Path
import subprocess
import sys

from build_firmware import ROOT, TOOLCHAIN, executable

sys.path.insert(0, str(ROOT / '.pio/testdeps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from test_buttons import read_symbols


def main():
    build = ROOT / '.pio/scratch/daily-dose-tests'
    build.mkdir(parents=True, exist_ok=True)
    elf, binary = build / 'test.elf', build / 'test.bin'
    subprocess.run([
        executable(TOOLCHAIN, 'arm-none-eabi-gcc'),
        '-mcpu=cortex-m0plus', '-mthumb', '-Os', '-flto',
        '-ffunction-sections', '-fdata-sections', '-ffreestanding', '-fno-builtin',
        '-Wall', '-Wextra', '-Werror', '-nostdlib', '-nostartfiles', '-specs=nano.specs',
        '-I', str(ROOT / 'include'), '-Wl,--gc-sections,-Ttext=0x10000,-e,test_entry',
        *[str(ROOT / path) for path in ('src/daily_dose.c', 'src/dose_reconcile.c',
                                        'src/log_record.c', 'tests/test_daily_dose.c')],
        '-lc', '-lgcc', '-o', str(elf),
    ], check=True)
    subprocess.run([executable(TOOLCHAIN, 'arm-none-eabi-objcopy'),
                    '-O', 'binary', str(elf), str(binary)], check=True)
    symbols = read_symbols(TOOLCHAIN, '.exe' if sys.platform == 'win32' else '', elf)
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    cpu.mem_map(0x10000, 0x20000)
    cpu.mem_write(0x10000, binary.read_bytes())
    cpu.mem_map(0x20000000, 0x2000)
    cpu.reg_write(UC_ARM_REG_SP, 0x20002000)
    cpu.reg_write(UC_ARM_REG_LR, 0x1FFF1)
    cpu.emu_start(symbols['test_entry'] | 1, 0x1FFF0, count=2000000)
    assert cpu.reg_read(UC_ARM_REG_PC) == 0x1FFF0, 'Regression did not finish'
    result = cpu.reg_read(UC_ARM_REG_R0)
    assert result == 0, f'Dose regression failed at test_daily_dose.c:{result}'
    print('PASS: reconciled DAY/TOTAL use exact history values, '
          'survive restart, and allow zero without redundant writes')


if __name__ == '__main__':
    main()
