"""Compare optimized display code with a saved firmware, on Cortex-M0+.

Usage: python tools/test_display_equivalence.py --baseline BUILD_DIR --build BUILD_DIR
Both directories must contain firmware.elf and firmware.bin.
"""
import argparse
from pathlib import Path
import random
import sys

sys.dont_write_bytecode = True
import test_graph_logs as graph
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP


def load(directory):
    graph.BUILD = Path(directory)
    return graph.Firmware()


def settings(fw, unit, lead, prefix):
    address = fw.symbols['lcd_settings_value_index']
    for offset, value in ((8, lead), (9, unit), (10, prefix), (11, prefix)):
        fw.cpu.mem_write(address + offset, bytes([value]))


def output(fw, function, *args):
    fw.cpu.mem_write(0x20010000, bytes([0xcc]) * 32)
    fw.call(function, *args)
    return bytes(fw.cpu.mem_read(0x20010000, 16))


def compare(old, new):
    checks = 0
    # All glyph pixels, cell edges, unsupported bytes and off-cell coordinates.
    for char in range(256):
        for function in ('font_large_has_glyph', 'font_large_glyph_width'):
            if function in old.symbols and function in new.symbols:
                assert old.call(function, char) == new.call(function, char), (function, char)
        coords = ((x, y) for y in range(35) for x in range(26)) if chr(char) in '0123456789:. ' else ((11, 10), (11, 30), (255, 255))
        for x, y in coords:
            assert old.call('font_large_pixel', char, x, y) == new.call('font_large_pixel', char, x, y), ('pixel', char, x, y)
            checks += 1
    rng = random.Random(20261027)
    values = {0, 1, 9, 10, 60, 61, 135, 136, 99999, 100000, 2**32-1}
    for power in range(1, 10):
        values.update(10**power + delta for delta in (-1, 0, 1))
    values.update(rng.randrange(2**32) for _ in range(30))
    for unit in range(3):
        for lead in range(2):
            for prefix in range(4):
                for fw in (old, new): settings(fw, unit, lead, prefix)
                for value in sorted(values):
                    old_unit = 4 if unit == 1 else prefix
                    args = (value, old_unit, 0x20010000)
                    assert output(old, 'lcd_graph_format_stat', *args) == output(new, 'lcd_graph_format_stat', *args), ('rate', unit, lead, prefix, value)
                    args = (value, 0, 0x20010000, unit)
                    assert output(old, 'lcd_graph_format_dose', *args) == output(new, 'lcd_graph_format_dose', *args), ('dose', unit, lead, prefix, value)
                    checks += 2
                for value in (2**32, 2**40, 2**48, 2**63-1):
                    args = (value & 0xffffffff, value >> 32, 0x20010000, unit)
                    assert output(old, 'lcd_graph_format_dose', *args) == output(new, 'lcd_graph_format_dose', *args), ('wide dose', unit, lead, prefix, value)
                    checks += 1
    print(f'PASS: {checks} comparisons of bitmap pixels and rate/dose output, including overflow and leading zeros')


def compare_menu(old, new):
    checks = 0
    for unit in range(3):
        for period in range(3):
            for item in range(29):
                for fw in (old, new):
                    settings(fw, unit, 0, 1)
                    fw.cpu.mem_write(fw.symbols['lcd_settings_value_index'] + 23, bytes([period]))
                count = old.call('lcd_settings_value_count', item)
                assert count == new.call('lcd_settings_value_count', item), ('menu count', item, unit, period)
                for value in range(count):
                    for editing in range(2):
                        texts = []
                        for fw in (old, new):
                            fw.set('lcd_settings_editing', editing)
                            fw.set('lcd_settings_selected', item)
                            fw.set('lcd_settings_edit_value', value)
                            fw.cpu.mem_write(fw.symbols['lcd_settings_value_index'] + item, bytes([value]))
                            address = fw.call('lcd_settings_value_text', item)
                            texts.append(bytes(fw.cpu.mem_read(address, 32)).split(b'\0')[0])
                        assert texts[0] == texts[1], ('menu text', unit, period, item, value, editing, texts)
                        checks += 1
    print(f'PASS: {checks} comparisons of menu labels/options, including editing and dependent units')


def compare_controller_init(old, new):
    snapshots = []
    for fw in (old, new):
        fw.reset()
        fw.set('lcd_started', 1)
        fw.set('lcd_init_state', 7)  # SLPOUT_DELAY: enqueue controller config.
        fw.call('board_display_service')
        count = fw.get('lcd_queue_tail', 2)
        assert 0 < count < 192
        snapshots.append((fw.get('lcd_init_state'), fw.get('lcd_init_deadline', 4),
            bytes(fw.cpu.mem_read(fw.symbols['lcd_queue'], count * 2))))
    assert snapshots[0] == snapshots[1], 'LCD controller command/payload sequence changed'
    print(f'PASS: identical LCD init queue ({len(snapshots[0][2]) // 2} command/data bytes), state and deadline')


def check_startup(fw):
    fw.reset()
    stack = int.from_bytes(fw.image[:4], 'little')
    reset = int.from_bytes(fw.image[4:8], 'little')
    assert stack == fw.symbols['_estack']
    assert reset == fw.symbols['Reset_Handler'] | 1
    fw.cpu.mem_write(0x20000000, bytes([0xa5]) * 8192)
    fw.cpu.reg_write(UC_ARM_REG_SP, stack)
    fw.cpu.emu_start(reset, fw.symbols['main'], count=100000)
    assert fw.cpu.reg_read(UC_ARM_REG_PC) == fw.symbols['main']
    start, end = fw.symbols['_sdata'], fw.symbols['_edata']
    offset = fw.symbols['_sidata'] - 0x08000000
    assert bytes(fw.cpu.mem_read(start, end-start)) == fw.image[offset:offset+end-start]
    start, end = fw.symbols['_sbss'], fw.symbols['_ebss']
    assert bytes(fw.cpu.mem_read(start, end-start)) == bytes(end-start)
    print('PASS: reset vector reaches main, initialized data copied, BSS cleared')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--build', required=True)
    args = parser.parse_args()
    old, new = load(args.baseline), load(args.build)
    compare(old, new)
    compare_menu(old, new)
    compare_controller_init(old, new)
    check_startup(new)
