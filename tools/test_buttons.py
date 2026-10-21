"""Run the actual Cortex-M0+ button input code in Unicorn, without a board.

Dependency (local, ignored):
  python -m pip install --target .pio/testdeps unicorn
Run:
  python tools/test_buttons.py
"""
from pathlib import Path
import argparse
import os
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".pio" / "testdeps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB
from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
                               UC_ARM_REG_SP)


def read_symbols(toolchain, suffix, elf):
    output = subprocess.check_output(
        [str(toolchain / f"arm-none-eabi-nm{suffix}"), str(elf)], text=True)
    return {fields[-1]: int(fields[0], 16) for line in output.splitlines()
            if len(fields := line.split()) == 3}


def test_driver(toolchain, suffix, build, elf):
    """Exercise the firmware's EXTI latch and GPIO adapter with emulated GPIO."""
    symbols = read_symbols(toolchain, suffix, elf)
    binary = build / "driver.bin"
    subprocess.run([str(toolchain / f"arm-none-eabi-objcopy{suffix}"),
                    "-O", "binary", str(elf), str(binary)], check=True)
    firmware = binary.read_bytes()
    emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    emulator.mem_map(0x08000000, 0x10000)
    emulator.mem_write(0x08000000, firmware)
    emulator.mem_map(0x20000000, 0x2000)
    emulator.mem_map(0x40010000, 0x1000)  # SYSCFG + EXTI
    emulator.mem_map(0x50000000, 0x2000)  # GPIOA + GPIOB
    data_size = symbols['_edata'] - symbols['_sdata']
    data_offset = symbols['_sidata'] - 0x08000000
    emulator.mem_write(symbols['_sdata'], firmware[data_offset:data_offset + data_size])

    def write32(address, value):
        emulator.mem_write(address, struct.pack('<I', value))

    def call(name, arg=0):
        emulator.reg_write(UC_ARM_REG_SP, 0x20002000)
        emulator.reg_write(UC_ARM_REG_LR, 0x0800FFF1)
        emulator.reg_write(UC_ARM_REG_R0, arg)
        emulator.emu_start(symbols[name] | 1, 0x0800FFF0, count=200000)
        assert emulator.reg_read(UC_ARM_REG_PC) == 0x0800FFF0, name
        return emulator.reg_read(UC_ARM_REG_R0)

    def raw(left=False, right=False):
        write32(0x50000010, 0 if right else 1 << 4)
        write32(0x50000410, 0 if left else 1 << 9)

    def sample(now, left=False, right=False):
        raw(left, right)
        write32(symbols['board_millis'], now)
        return call('board_buttons_sample_10ms')

    for pin, down, up, click in ((4, 32, 128, 2), (9, 16, 64, 1)):
        # The ISR must retain even a tap released before the main loop runs.
        raw()
        write32(0x40010414, 1 << pin)
        call('EXTI4_15_IRQHandler')
        assert call('board_take_button_wakeup_request') == down
        assert call('board_take_button_wakeup_request') == 0
        write32(symbols['board_millis'], 0)
        call('board_buttons_suppress_until_release', down)
        for time in (0, 10, 20):
            assert sample(time) == 0
        key = {'right': True} if pin == 4 else {'left': True}
        assert sample(30, **key) == 0
        assert sample(50, **key) == down
        assert sample(100) == 0
        assert sample(120) == up | click

    # A left/right combination must survive the shared EXTI IRQ atomically.
    write32(0x40010414, (1 << 4) | (1 << 9))
    call('EXTI4_15_IRQHandler')
    assert call('board_take_button_wakeup_request') == 48
    assert call('board_take_button_wakeup_request') == 0
    print('PASS: firmware EXTI/GPIO paths for both buttons and simultaneous wake')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path,
                        help='Also test the board adapter from this built firmware ELF')
    args = parser.parse_args()
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    toolchain = core / "packages" / "toolchain-gccarmnoneeabi" / "bin"
    suffix = ".exe" if os.name == "nt" else ""
    build = ROOT / ".pio" / "button-tests"
    build.mkdir(parents=True, exist_ok=True)
    elf = build / "buttons.elf"
    binary = build / "buttons.bin"
    subprocess.run([
        str(toolchain / f"arm-none-eabi-gcc{suffix}"),
        "-mcpu=cortex-m0plus", "-mthumb", "-Os", "-g", "-std=c11",
        "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
        "-nostdlib", "-nostartfiles", "-I", str(ROOT / "include"),
        "-Wl,-Ttext=0x10000,-e,test_entry",
        str(ROOT / "src" / "button_input.c"),
        str(ROOT / "tests" / "test_button_input.c"), "-o", str(elf)
    ], check=True)
    subprocess.run([str(toolchain / f"arm-none-eabi-objcopy{suffix}"),
                    "-O", "binary", str(elf), str(binary)], check=True)
    entry = read_symbols(toolchain, suffix, elf)['test_entry']
    emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    emulator.mem_map(0x10000, 0x10000)
    emulator.mem_write(0x10000, binary.read_bytes())
    emulator.mem_map(0x20000000, 0x2000)
    emulator.reg_write(UC_ARM_REG_SP, 0x20002000)
    emulator.reg_write(UC_ARM_REG_LR, 0x1F001)
    emulator.emu_start(entry | 1, 0x1F000, count=2000000)
    assert emulator.reg_read(UC_ARM_REG_PC) == 0x1F000, 'Test execution did not finish'
    result = emulator.reg_read(UC_ARM_REG_R0)
    if result:
        raise SystemExit(f"FAIL: tests/test_button_input.c:{result}")
    print("PASS: 15 button scenarios executed as Cortex-M0+ machine code")
    if args.firmware:
        test_driver(toolchain, suffix, build, args.firmware.resolve())


if __name__ == "__main__":
    main()
