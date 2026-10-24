"""Exercise built firmware log storage and graph navigation in Unicorn.

python -m pip install --target .pio/testdeps unicorn
python tools/test_graph_logs.py
EEPROM/NOR and peripheral completion are simulated; firmware code is unchanged.
"""
from pathlib import Path
import os
import random
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('GEIGER2_TEST_BUILD_DIR',
                            str(ROOT / '.pio/build/geiger2')))
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / '.pio' / 'testdeps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_PC, UC_ARM_REG_LR,
                               UC_ARM_REG_SP)
from test_buttons import read_symbols


class Firmware:
    def __init__(self, stack_reserve=0):
        self.stack_top = 0x20002000 - stack_reserve
        self.symbols = read_symbols(Path.home() / '.platformio/packages/'
                                   'toolchain-gccarmnoneeabi/bin', '.exe',
                                   BUILD / 'firmware.elf')
        self.image = (BUILD / 'firmware.bin').read_bytes()
        self.eeprom = bytearray(b'\xff' * 2048)
        self.nor = bytearray(b'\xff' * 0x100000)
        self.spi_ready = True
        self.spi_online = True
        self.spi_read_calls = 0
        self.program_failures = 0
        self.eeprom_log_area_writes = 0
        self.eeprom_checkpoint_writes = 0
        self.eeprom_cutoff_writes = 0
        self.reset()

    def reset(self):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for base, size in ((0x08000000, 0x10000), (0x20000000, 0x2000),
                           (0x20010000, 0x1000), (0x40000000, 0x30000),
                           (0x50000000, 0x2000), (0xe000e000, 0x1000)):
            self.cpu.mem_map(base, size)
        self.cpu.mem_write(0x08000000, self.image)
        start, end = self.symbols['_sdata'], self.symbols['_edata']
        offset = self.symbols['_sidata'] - 0x08000000
        self.cpu.mem_write(start, self.image[offset:offset + end - start])
        names = ('board_eeprom_read', 'board_eeprom_write', 'spi_nor_is_ready',
                 'spi_nor_init', 'spi_nor_read', 'spi_nor_program_page',
                 'spi_nor_erase_sector', 'lcd_check_output_level')
        self.hooks = {self.symbols[name]: name for name in names}
        for address in self.hooks:
            self.cpu.hook_add(UC_HOOK_CODE, self.stub, begin=address, end=address)

    def stub(self, cpu, address, size, user):
        name = self.hooks[address]
        a, b, c = (cpu.reg_read(r) for r in
                   (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
        result = 1
        if name == 'board_eeprom_read':
            cpu.mem_write(b, bytes([self.eeprom[a]]))
        elif name == 'board_eeprom_write':
            self.eeprom[a] = b & 255
            if 205 <= a < 1405:
                self.eeprom_log_area_writes += 1
            if 1406 <= a < 1566:
                self.eeprom_checkpoint_writes += 1
            if 140 <= a < 164 or 181 <= a < 205:
                self.eeprom_cutoff_writes += 1
        elif name == 'spi_nor_init':
            self.spi_ready = self.spi_online
            result = int(self.spi_ready)
        elif name == 'spi_nor_is_ready':
            result = int(self.spi_ready)
        elif name == 'spi_nor_read':
            self.spi_read_calls += 1
            cpu.mem_write(b, bytes(self.nor[a:a + c]))
        elif name == 'spi_nor_program_page':
            if self.program_failures:
                self.program_failures -= 1
                result = 0
            else:
                for index, byte in enumerate(cpu.mem_read(b, c)):
                    self.nor[a + index] &= byte
        elif name == 'spi_nor_erase_sector':
            self.nor[a:a + 4096] = b'\xff' * 4096
        cpu.reg_write(UC_ARM_REG_R0, result)
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def call(self, name, *args):
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            self.cpu.reg_write(reg, value)
        self.cpu.reg_write(UC_ARM_REG_SP, self.stack_top)
        self.cpu.reg_write(UC_ARM_REG_LR, 0x0800fff1)
        self.cpu.emu_start(self.symbols[name] | 1, 0x0800fff0, count=2000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == 0x0800fff0, name + ' stalled'
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def set(self, name, value, size=1):
        self.cpu.mem_write(self.symbols[name], value.to_bytes(size, 'little'))

    def get(self, name, size=1):
        return int.from_bytes(self.cpu.mem_read(self.symbols[name], size), 'little')

    def write32(self, address, value):
        self.cpu.mem_write(address, struct.pack('<I', value))

    def logs(self, period, capacity=87, offset=None):
        # Arguments beyond r0-r3 are placed on the caller's stack.
        args = (period, 0x20010000, 0x20010200, capacity)
        stack_args = (0x20010500, 0)
        name = 'log_storage_graph_load'
        if offset is not None:
            name = 'log_storage_graph_page'
            args = (period, offset, 0x20010000, 0x20010200)
            stack_args = (capacity, 0x20010500)
        self.cpu.mem_write(self.stack_top - 8, struct.pack('<II', *stack_args))
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3), args):
            self.cpu.reg_write(reg, value)
        self.cpu.reg_write(UC_ARM_REG_SP, self.stack_top - 8)
        self.cpu.reg_write(UC_ARM_REG_LR, 0x0800fff1)
        self.cpu.emu_start(self.symbols[name] | 1,
                           0x0800fff0, count=2000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == 0x0800fff0
        assert self.cpu.reg_read(UC_ARM_REG_R0), 'Log read failed'
        count = self.cpu.mem_read(0x20010500, 1)[0]
        return list(struct.unpack('<' + 'I' * count,
                                  self.cpu.mem_read(0x20010000, count * 4)))

    def logs_available(self, period):
        self.cpu.mem_write(self.stack_top - 8, struct.pack('<II', 0x20010500, 0))
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3),
                              (period, 0x20010000, 0x20010200, 87)):
            self.cpu.reg_write(reg, value)
        self.cpu.reg_write(UC_ARM_REG_SP, self.stack_top - 8)
        self.cpu.reg_write(UC_ARM_REG_LR, 0x0800fff1)
        self.cpu.emu_start(self.symbols['log_storage_graph_load'] | 1,
                           0x0800fff0, count=2000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == 0x0800fff0
        return bool(self.cpu.reg_read(UC_ARM_REG_R0))

    def log_counts(self):
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2),
                              (0x20010500, 0x20010502, 0x20010504)):
            self.cpu.reg_write(reg, value)
        self.cpu.reg_write(UC_ARM_REG_SP, self.stack_top)
        self.cpu.reg_write(UC_ARM_REG_LR, 0x0800fff1)
        self.cpu.emu_start(self.symbols['log_storage_log_counts'] | 1,
                           0x0800fff0, count=2000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == 0x0800fff0
        return tuple(int.from_bytes(self.cpu.mem_read(address, 2), 'little')
                     for address in (0x20010500, 0x20010502, 0x20010504))

    def minute(self, pulses=1):
        self.call('log_storage_sample_cpm', pulses * 60)
        for _ in range(60):
            self.set('board_millis', self.get('board_millis', 4) + 1000, 4)
            self.call('log_storage_second', pulses)

    def display(self):
        for name in ('lcd_started', 'lcd_ready', 'lcd_spi_ready',
                     'backlight_requested_on'):
            self.set(name, 1)
        self.set('lcd_init_state', 15)
        self.set('lcd_screen_page', 1)
        self.write32(0x40003808, 3)  # SPI2 TXE and RXNE.
        self.call('board_display_graph_enter')

    def service(self, complete=True):
        self.set('board_millis', self.get('board_millis', 4) + 1, 4)
        self.write32(0x40020000, (1 << 17) if complete and
                     self.get('lcd_dma_active') else 0)
        self.call('board_display_service')

    def drain(self):
        for _ in range(1500):
            self.service()
            if not self.get('lcd_graph_input_locked') and not self.get('lcd_graph_pending_action'):
                return
        names = ('lcd_graph_input_locked', 'lcd_graph_pending_action',
                 'lcd_dma_active', 'lcd_text_active', 'lcd_text_glyph_index',
                 'lcd_text_row_commands_queued', 'lcd_spi_ready',
                 'lcd_graph_range_index', 'lcd_cpm_redraw_pending')
        raise AssertionError('Graph input stayed locked: ' +
                             str({name: self.get(name) for name in names}))


def test_logs():
    fw = Firmware()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9700, 1)
    fw.minute()
    assert fw.logs(0) == [0, 60], 'Current RAM minute must precede SPI history'
    fw.call('log_storage_calendar_day', 9701, 1)
    fw.minute(2)
    assert fw.logs(2) == [120, 60], \
        'EEPROM daily history and current day missing or out of order'
    assert fw.log_counts()[2] == 1, \
        'DEBUG DAY must count EEPROM history only, excluding the open RAM day'
    assert fw.eeprom_log_area_writes > 0, \
        'SPI mode did not retain the completed day in EEPROM'
    fw.reset()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9701, 1)
    fw.minute()
    assert fw.logs(0) == [0, 60, 120, 60], 'SPI history lost after restart'
    assert fw.logs(2) == [60, 60], \
        'EEPROM daily history or current-day recovery failed after restart'
    print('PASS: SPI minutes/hours, EEPROM days, restart continuity')

    fw = Firmware()
    fw.call('log_storage_init', 1)
    for _ in range(59):
        fw.minute()
    assert fw.eeprom_checkpoint_writes == 0, \
        'EEPROM checkpoint was written before an hour elapsed'
    fw.minute()
    assert fw.eeprom_checkpoint_writes == 0, \
        'Hour rollover must not bypass the DAY checkpoint scheduler'
    assert fw.log_counts() == (60, 1, 0), \
        'SPI storage must retain 60 minute records and one hour, no days yet'
    assert fw.eeprom_log_area_writes == 0, \
        'SPI hourly rollover wrote a day log before the calendar boundary'
    fw.minute()
    assert fw.eeprom_checkpoint_writes == 0, \
        'Closing SPI minutes must not write EEPROM checkpoints'
    print('PASS: hourly SPI rollover leaves EEPROM writes to its scheduler')

    fw = Firmware()
    fw.call('log_storage_init', 0)
    fw.call('log_storage_calendar_day', 9700, 1)
    fw.minute(3)
    fw.call('log_storage_calendar_day', 9701, 1)
    assert fw.log_counts() == (0, 0, 1), \
        'EEPROM-only mode must retain daily logs only'
    assert fw.logs(2) == [0, 180], 'EEPROM daily graph lost the completed day'
    fw.display()
    assert fw.get('lcd_graph_range_index') == 3, \
        'EEPROM-only graph navigation must retain the seconds graph'
    print('PASS: EEPROM-only stores daily history without minute/hour logs')

    fw = Firmware()
    fw.call('log_storage_init', 0)
    fw.call('log_storage_time_cutoff', 123, 456, 1, 1)
    assert fw.eeprom[140 + 23] == 0xC1 and fw.eeprom[140 + 10] & 0x80, \
        'BOOT cutoff was not saved to EEPROM with valid calendar time'
    assert fw.eeprom[140 + 21] == 0, \
        'EEPROM cutoff did not record SPI-disabled state'
    fw.call('log_storage_time_cutoff', 234, 567, 1, 2)
    assert fw.eeprom[181 + 23] == 0xC1 and fw.eeprom[181 + 10] & 0x7F == 2, \
        'CLOCK_SET cutoff was not saved in the alternating EEPROM slot'
    assert fw.eeprom_cutoff_writes > 0, \
        'Control events did not write to EEPROM'
    print('PASS: EEPROM cutoff events persist with SPI disabled')

    fw = Firmware()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9700, 1)
    for _ in range(12):
        fw.call('log_storage_sample_cpm', 60)
        fw.call('log_storage_second', 1)
    # USB makes SPI snapshots due every 10 seconds.  They must survive a
    # reset only when their RTC period key still matches.
    fw.call('log_storage_checkpoint', 9700 * 86400 + 12, 1, 1, 0)
    fw.reset()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9700, 1)
    fw.call('log_storage_restore_live', 9700 * 86400 + 12, 1)
    assert fw.get('minute_dose', 4) == 12 and fw.get('minute_seconds') == 12, \
        'SPI LIVE_MIN did not restore the current dated minute'
    fw.call('log_storage_restore_live', 9700 * 86400 + 60, 1)
    assert fw.get('minute_dose', 4) == 12, \
        'A live minute was incorrectly accepted for another minute'
    print('PASS: dated SPI live minute survives only its matching period')

    fw = Firmware()
    fw.call('log_storage_init', 0)
    fw.call('log_storage_calendar_day', 2000, 1)
    for day in range(100):
        fw.minute()
        fw.call('log_storage_calendar_day', 2001 + day, 1)
    assert fw.log_counts() == (0, 0, 100), \
        'EEPROM daily ring did not cap at 100 entries'
    assert len(fw.logs(2, 100)) == 100, \
        'Daily graph did not expose all 100 EEPROM entries'
    print('PASS: EEPROM daily ring and graph retain at most 100 days')


def test_graph():
    fw = Firmware()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9700, 1)
    fw.minute()
    fw.set('settings_spi_enabled', 1)
    fw.display()
    fw.drain()
    fw.call('board_display_right_short')  # Log views are temporarily hidden.
    fw.drain()
    assert fw.get('lcd_graph_range_index') == 3
    rng = random.Random(7)
    actions = ('board_display_left_short', 'board_display_right_short',
               'board_display_graph_long_press')
    for _ in range(250):
        fw.call(rng.choice(actions))
        for _ in range(rng.randrange(1, 8)):
            fw.service()
            assert fw.get('lcd_spi_ready'), 'Rapid input broke the LCD queue'
    fw.drain()
    fw.call('board_display_graph_back')
    fw.drain()
    print('PASS: 250 rapid graph actions and deferred back')
    # A missing DMA completion must not lock the graph forever.
    fw.call('board_display_graph_long_press')
    for _ in range(200):
        fw.service(complete=False)
    fw.call('board_display_left_short')
    fw.drain()
    print('PASS: graph recovers after a missing DMA completion')


def test_seconds_only_navigation():
    for spi in (0, 1):
        for saved_mode in range(4, 8):
            fw = Firmware(stack_reserve=600)
            fw.call('log_storage_init', spi)
            fw.set('settings_spi_enabled', spi)
            fw.set('lcd_graph_range_index', saved_mode)
            reads = fw.spi_read_calls
            fw.display()
            fw.drain()
            assert fw.get('lcd_graph_range_index') == 3, \
                'A remembered log view did not return to 90s'
            for mode in range(4):
                fw.call('board_display_right_short')
                fw.call('board_display_left_short')
                fw.drain()
                fw.call('board_display_graph_long_press')
                fw.drain()
                assert fw.get('lcd_graph_range_index') == mode, \
                    'Long right did not select the next seconds window'
                assert not fw.get('lcd_graph_selected_active')
            assert fw.spi_read_calls == reads, \
                'Seconds-only navigation read SPI history'
            fw.minute()
            assert fw.log_counts() == ((1, 0, 0) if spi else (0, 0, 0)), \
                'Hiding log views stopped the storage writer'
    print('PASS: seconds-only UI, restored window, SPI on/off, logging continues')


def test_live_graphs():
    fw = Firmware()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9700, 1)
    reads_before_live_hour = fw.spi_read_calls
    assert fw.logs(0) == [0] and fw.logs(1) == [0], \
        'Fresh MIN/HOUR graphs must expose the open RAM period'
    assert fw.spi_read_calls == reads_before_live_hour, \
        'A RAM-only current hour must not trigger an SPI history read'
    assert fw.log_counts() == (0, 0, 0), \
        'Empty history must stay zero despite existing live buffers'
    fw.minute(2)
    for _ in range(7):
        fw.call('log_storage_second', 3)
    fw.call('log_storage_sample_cpm', 180)
    assert fw.logs(0) == [21, 120], \
        'Minute graph must show live dose first and history exactly once'
    assert fw.logs(1) == [141] and fw.logs(2) == [141], \
        'Hour/day live bars must include the unclosed minute'
    assert fw.log_counts() == (1, 0, 0), \
        'Live hour/day bars must not increase persisted history counts'
    assert fw.call('log_storage_graph_current_seconds', 0) == 7
    assert fw.call('log_storage_graph_current_seconds', 1) == 67
    assert fw.logs(0, 1) == [21], 'A one-bar graph must retain the live bar'
    for _ in range(59):
        fw.minute()
    # There is one completed hour and seven seconds of the current minute.
    assert fw.logs(1)[0] == fw.get('hour_dose', 4) + fw.get('minute_dose', 4)
    assert len(fw.logs(1)) == 2, 'Live hour must precede one completed hour'
    print('PASS: live-first MIN/HOUR/DAY export and partial periods')


def test_spi_retry():
    fw = Firmware()
    fw.spi_ready = fw.spi_online = False
    fw.call('log_storage_init', 1)
    assert not fw.call('log_storage_spi_available')
    fw.spi_online = True
    fw.call('log_storage_calendar_day', 9700, 1)
    fw.minute()
    assert fw.call('log_storage_spi_available')
    assert fw.logs(0) == [0, 60], 'Late SPI startup failed to resume minute writes'
    fw.program_failures = 1
    fw.minute()
    assert not fw.call('log_storage_spi_available'), 'Write failure not reported'
    assert fw.logs(0) == [0, 60], 'Write failure hid existing SPI history'
    fw.minute()
    assert fw.call('log_storage_spi_available'), 'SPI logging did not recover'
    assert fw.logs(0) == [0, 60, 60], 'Recovered SPI record lost'
    print('PASS: delayed SPI startup, read history after failed write, retry')


def test_sparse_graph_history():
    def crc8(data):
        value = 0
        for byte in data:
            value ^= byte
            for _ in range(8):
                value = ((value << 1) ^ (7 if value & 128 else 0)) & 255
        return value

    fw = Firmware()
    fw.call('log_storage_init', 1)
    # Use the firmware writer for 20 MIN/HOUR records, without waiting hours.
    for dose in range(1, 21):
        fw.set('minute_dose', dose - 1, 4)
        fw.set('minute_seconds', 59)
        fw.set('hour_dose', dose * 100 - dose, 4)
        fw.set('hour_minutes', 59)
        fw.call('log_storage_sample_cpm', 60)
        fw.call('log_storage_second', 1)
    # Emulate a damaged intermediate sector. Newest MIN holds four records;
    # newest HOUR holds only a day marker. Older history is still valid.
    for base, records in ((0, 4), (176, 0)):
        older = base * 4096
        newer = (base + 2) * 4096
        header = bytearray(fw.nor[older:older + 16])
        header[5:9] = (2).to_bytes(4, 'little')
        header[9] = crc8(header[:9])
        fw.nor[newer:newer + 16] = header
        if records:
            fw.nor[newer + 16:newer + 16 + records * 11] = \
                fw.nor[older + 16:older + 16 + records * 11]
        else:
            marker = bytearray(11)
            marker[9] = 0xC0
            marker[10] = crc8(marker[:10])
            fw.nor[newer + 16:newer + 27] = marker
    fw.reset()
    fw.call('log_storage_init', 1)
    original_nor = bytes(fw.nor)
    minutes = fw.logs(0)
    hours = fw.logs(1)
    expected_minutes = [0, 4, 3, 2, 1] + list(range(20, 0, -1))
    expected_hours = [0] + list(range(2000, 0, -100))
    assert minutes == expected_minutes and hours == expected_hours, \
        f'Sector gap hid history: MIN {len(minutes)}, HOUR {len(hours)}'
    assert fw.logs(0, 3, offset=5) == expected_minutes[5:8]
    assert fw.logs(1, 3, offset=1) == expected_hours[1:4]
    assert bytes(fw.nor) == original_nor, 'Graph loading changed persistent data'
    # A reset after writing only the new sector header must also retain history.
    for base in (0, 176):
        start = (base + 2) * 4096 + 16
        fw.nor[start:start + 4080] = b'\xff' * 4080
    fw.reset()
    fw.call('log_storage_init', 1)
    assert fw.logs(0) == [0] + list(range(20, 0, -1))
    assert fw.logs(1) == expected_hours
    assert fw.logs(0, 3, offset=21) == [], 'Pagination repeated exhausted history'
    print('PASS: MIN/HOUR history and pagination survive gaps between sectors')


def test_hour_bar_color():
    fw = Firmware()
    fw.set('lcd_graph_range_index', 5)
    fw.set('lcd_graph_log_visible_count', 2)
    fw.cpu.mem_write(fw.symbols['lcd_graph_column_y'], bytes([15]) * 160)
    fw.set('lcd_text_glyph_index', 1)
    fw.call('lcd_graph_prepare_glyph')
    pixel = fw.symbols['lcd_text_glyph'] + 53 * 2
    assert int.from_bytes(fw.cpu.mem_read(pixel, 2), 'little') == 0xBDF7, \
        'Unselected current HOUR bar is not gray'
    fw.set('lcd_graph_selected_active', 1)
    fw.call('lcd_graph_prepare_glyph')
    assert int.from_bytes(fw.cpu.mem_read(pixel, 2), 'little') == 0x07EE, \
        'Selected current HOUR bar lost the cursor accent'
    print('PASS: current HOUR bar is gray and uses the cursor accent on selection')


def test_calendar_hour_boundary():
    fw = Firmware()
    fw.call('log_storage_init', 1)
    start = 9700 * 86400 + 3590
    fw.call('log_storage_restore_live', start, 1)
    for second in range(1, 11):
        # Follow the main loop: RTC/checkpoint first, pulse aggregation second.
        fw.call('log_storage_checkpoint', start + second, 1, 0, 0)
        fw.call('log_storage_sample_cpm', 60)
        fw.call('log_storage_second', 1)
    assert fw.logs(1) == [1, 9], \
        'RTC hour boundary did not move the closed hour into SPI history'
    assert fw.log_counts()[1] == 1, 'Closed RTC hour is missing from physical logs'
    print('PASS: RTC hour boundary persists the old hour and starts a new RAM bar')


if __name__ == '__main__':
    test_logs()
    test_live_graphs()
    test_spi_retry()
    test_graph()
    test_seconds_only_navigation()
    test_sparse_graph_history()
    test_hour_bar_color()
    test_calendar_hour_boundary()
