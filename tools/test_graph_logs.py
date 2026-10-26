"""Exercise the retained one-second live log and SEC graph in Unicorn.

Run after building: python tools/test_graph_logs.py
"""
from pathlib import Path
import os
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('GEIGER2_TEST_BUILD_DIR', str(ROOT / '.pio/build/geiger2')))
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / '.pio' / 'testdeps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP
from test_buttons import read_symbols

class Firmware:
    def __init__(self):
        self.symbols = read_symbols(Path.home() / '.platformio/packages/toolchain-gccarmnoneeabi/bin', '.exe', BUILD / 'firmware.elf')
        self.image = (BUILD / 'firmware.bin').read_bytes()
        self.reset()

    def reset(self):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for base, size in ((0x08000000, 0x10000), (0x20000000, 0x2000), (0x20010000, 0x1000), (0x40000000, 0x30000), (0x50000000, 0x2000), (0xe000e000, 0x1000)):
            self.cpu.mem_map(base, size)
        self.cpu.mem_write(0x08000000, self.image)
        start, end = self.symbols['_sdata'], self.symbols['_edata']
        offset = self.symbols['_sidata'] - 0x08000000
        self.cpu.mem_write(start, self.image[offset:offset + end - start])

    def call(self, name, *args):
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args): self.cpu.reg_write(reg, value)
        self.cpu.reg_write(UC_ARM_REG_SP, 0x20002000)
        self.cpu.reg_write(UC_ARM_REG_LR, 0x0800fff1)
        self.cpu.emu_start(self.symbols[name] | 1, 0x0800fff0, count=1000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == 0x0800fff0, name + ' stalled'
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def get(self, name, size=1): return int.from_bytes(self.cpu.mem_read(self.symbols[name], size), 'little')
    def set(self, name, value, size=1): self.cpu.mem_write(self.symbols[name], value.to_bytes(size, 'little'))


def test_second_ring():
    fw = Firmware()
    fw.call('second_log_init')
    assert fw.call('second_log_count') == 0
    for value in range(1, 96): fw.call('second_log_add', value)
    assert fw.call('second_log_count') == 90
    assert fw.call('second_log_get', 0, 0x20010000)
    assert int.from_bytes(fw.cpu.mem_read(0x20010000, 4), 'little') == 95
    assert fw.call('second_log_get', 89, 0x20010000)
    assert int.from_bytes(fw.cpu.mem_read(0x20010000, 4), 'little') == 6
    assert not fw.call('second_log_get', 90, 0x20010000)
    fw.call('second_log_reset_rate_window')
    cpm = fw.call('second_log_add', 2)
    assert cpm == 120
    print('PASS: 90-record SEC ring, newest-first reads, rolling CPM reset')


def test_graph_navigation():
    fw = Firmware()
    assert fw.get('lcd_graph_range_index') == 3
    fw.set('lcd_screen_page', 5)
    fw.set('lcd_graph_input_locked', 0)
    for expected in (0, 1, 2, 3, 0):
        assert fw.call('board_display_graph_long_press')
        assert fw.get('lcd_graph_range_index') == expected
        fw.set('lcd_graph_input_locked', 0)
    assert not any(name in fw.symbols for name in ('log_storage_second', 'log_storage_graph_load', 'log_storage_alert_sample'))
    for expected in (1, 2, 3, 4, 0):
        fw.set('lcd_graph_input_locked', 0)
        fw.call('board_display_right_short')
        assert fw.get('lcd_graph_log_type') == expected
    print('PASS: independent SEC time windows and MIN/HOUR/DAY/ALERT graph navigation')


if __name__ == '__main__':
    test_second_ring()
    test_graph_navigation()
