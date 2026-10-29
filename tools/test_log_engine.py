"""Exercise the compiled ARM log engine with EEPROM/SPI and power-cut models.

python tools/build_firmware.py --test-support
python tools/test_log_engine.py
"""
from pathlib import Path
import binascii
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / '.pio/testdeps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP
from test_buttons import read_symbols

BUILD = ROOT / '.pio/scratch/geiger2_test'
TOOLS = Path.home() / '.platformio/packages/toolchain-gccarmnoneeabi/bin'
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
SCRATCH = 0x20010000
YEAR = 0x20 | (2026 & 31)
START = 845769600  # overwritten below from a calendar conversion
from datetime import datetime, timezone
START = int((datetime(2026, 10, 22, tzinfo=timezone.utc) - datetime(2000, 1, 1, tzinfo=timezone.utc)).total_seconds())


class PowerCut(Exception):
    pass


class Engine:
    symbols = None

    def __init__(self, mode=0, eeprom=None, nor=None):
        if self.symbols is None:
            Engine.symbols = read_symbols(TOOLS, '.exe', BUILD / 'firmware.elf')
        self.eeprom = bytearray(eeprom if eeprom is not None else b'\xff' * 2048)
        self.nor = bytearray(nor if nor is not None else b'\xff' * 49152)
        self.mode = self.source = mode
        self.now, self.uptime, self.valid, self.level = START, 0, True, 2
        self.ready = mode == 1
        self.cut = None
        self.writes = 0
        self.spi_cut = None
        self.spi_writes = 0
        self.tx = bytearray()
        self.minimum_sp = 0x20002000
        self.reset()

    def reset(self):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for base, size in ((0x08000000, 0x10000), (0x20000000, 0x2000),
                           (SCRATCH, 0x4000), (0x40000000, 0x30000),
                           (0x50000000, 0x2000), (0xe000e000, 0x1000)):
            self.cpu.mem_map(base, size)
        image = (BUILD / 'firmware.bin').read_bytes()
        self.cpu.mem_write(0x08000000, image)
        begin, end = self.symbols['_sdata'], self.symbols['_edata']
        offset = self.symbols['_sidata'] - 0x08000000
        self.cpu.mem_write(begin, image[offset:offset + end - begin])
        hooks = {
            'board_display_log_memory': lambda a: self.mode,
            'board_display_log_source': lambda a: self.source,
            'board_display_rate_led_mask': lambda a: self.level,
            'board_eeprom_read': self.read_eeprom,
            'board_eeprom_write': self.write_eeprom,
            'board_rtc_log_time': self.clock,
            'spi_nor_is_ready': lambda a: int(self.ready),
            'spi_nor_size_bytes': lambda a: len(self.nor),
            'spi_nor_init': self.init_spi,
            'spi_nor_read': self.read_spi,
            'spi_nor_program_page': self.program_spi,
            'spi_nor_erase_sector': self.erase_spi,
        }
        for name, fn in hooks.items():
            if name not in self.symbols:
                raise AssertionError('Missing test hook: ' + name)
            def callback(cpu, address, size, data, fn=fn):
                self.minimum_sp = min(self.minimum_sp, cpu.reg_read(UC_ARM_REG_SP))
                args = [cpu.reg_read(r) for r in REGS]
                result = fn(args)
                cpu.reg_write(UC_ARM_REG_R0, result)
                cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
            address = self.symbols[name] & ~1
            self.cpu.hook_add(UC_HOOK_CODE, callback, begin=address, end=address)
        self.cpu.mem_write(0x4001381c, struct.pack('<I', 1 << 7))
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, lambda cpu, access, address, size, value, data:
                          self.tx.append(value & 255), begin=0x40013828, end=0x40013828)

    def read_eeprom(self, a):
        assert a[0] < 2048
        self.cpu.mem_write(a[1], bytes([self.eeprom[a[0]]]))
        return 1

    def write_eeprom(self, a):
        assert a[0] < 2048
        if self.cut == self.writes:
            raise PowerCut()
        self.eeprom[a[0]] = a[1] & 255
        self.writes += 1
        return 1

    def clock(self, a):
        for ptr, value in zip(a[:3], (self.uptime, self.now, int(self.valid))):
            self.cpu.mem_write(ptr, struct.pack('<I', value) if ptr != a[2] else bytes([value]))
        return 1

    def init_spi(self, a):
        self.ready = True
        return 1

    def read_spi(self, a):
        address, ptr, length = a[:3]
        assert address + length <= len(self.nor)
        self.cpu.mem_write(ptr, bytes(self.nor[address:address + length]))
        return 1

    def program_spi(self, a):
        address, ptr, length = a[:3]
        assert address + length <= len(self.nor)
        if length == 0 or length > 256 or address // 256 != (address + length - 1) // 256:
            return 0
        data = bytes(self.cpu.mem_read(ptr, length))
        for i, value in enumerate(data):
            if self.spi_cut == self.spi_writes: raise PowerCut()
            self.spi_writes += 1
            assert self.nor[address + i] & value == value, 'NOR attempted 0 -> 1'
            self.nor[address + i] &= value
        return 1

    def erase_spi(self, a):
        address = a[0]
        assert address % 4096 == 0 and address + 4096 <= len(self.nor)
        self.nor[address:address + 4096] = b'\xff' * 4096
        return 1

    def call(self, name, *args):
        sp = 0x20001fc0
        for reg, value in zip(REGS, args): self.cpu.reg_write(reg, value)
        if len(args) > 4:
            self.cpu.mem_write(sp, struct.pack('<' + 'I' * (len(args) - 4), *args[4:]))
        self.cpu.reg_write(UC_ARM_REG_SP, sp)
        self.cpu.reg_write(UC_ARM_REG_LR, 0x0800fff1)
        self.cpu.emu_start(self.symbols[name] | 1, 0x0800fff0, count=100_000_000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == 0x0800fff0, name + ' did not return'
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def get(self, name, size=1):
        return int.from_bytes(self.cpu.mem_read(self.symbols[name], size), 'little')

    def set(self, name, value, size=1):
        self.cpu.mem_write(self.symbols[name], value.to_bytes(size, 'little'))

    def append(self, kind, index, count, maximum=60, duration=0, epoch=0):
        self.cpu.mem_write(SCRATCH, struct.pack('<IIIHBx', index, count, maximum, duration, epoch % 64))
        if self.mode == 1:
            result = self.call('spi_log_prepare', kind, epoch, YEAR, SCRATCH + 128)
            assert result == 0, result
            assert self.call('spi_log_append', kind, epoch, SCRATCH)
        else:
            assert self.call('log_eeprom_append', kind, epoch, SCRATCH, YEAR)

    def points(self, kind, period, limit=8, date_limit=0):
        cursor, point = SCRATCH + 256, SCRATCH + 320
        self.call('log_history_begin', cursor)
        result = []
        for _ in range(limit):
            if not self.call('log_history_next', kind, period, cursor, date_limit, point): break
            fields = struct.unpack('<6I2H4B', self.cpu.mem_read(point, 32))
            result.append(dict(zip(('time', 'period', 'count', 'avg', 'max', 'covered', 'epoch', 'id', 'calendar', 'available', 'partial', 'live'), fields)))
        return result


def test_codec_and_time():
    fw = Engine()
    for kind, count, duration in ((0, 262144, 0), (1, 262144 * 60, 0),
                                  (2, 262144 * 1440, 0), (3, 262144 * 4096 // 60, 4096)):
        fw.cpu.mem_write(SCRATCH, struct.pack('<IIIHBx', 0, count, 262144, duration, 63))
        size = 11 if kind == 3 else 8
        assert fw.call('log_record_encode', kind, SCRATCH, SCRATCH + 32, size)
        assert fw.call('log_record_decode', kind, SCRATCH + 32, size, SCRATCH + 64)
        assert fw.cpu.mem_read(SCRATCH, 15) == fw.cpu.mem_read(SCRATCH + 64, 15)
    for year in (2000, 2026, 2032, 2060, 2099):
        ref = int((datetime(year, 7, 1, tzinfo=timezone.utc) - datetime(2000, 1, 1, tzinfo=timezone.utc)).total_seconds())
        assert fw.call('log_record_time', 0, 0, 0x20 | (year & 31), ref, SCRATCH)
        expected = int((datetime(year, 1, 1, tzinfo=timezone.utc) - datetime(2000, 1, 1, tzinfo=timezone.utc)).total_seconds())
        assert int.from_bytes(fw.cpu.mem_read(SCRATCH, 4), 'little') == expected
    print('PASS: exact CNT/CPM maxima, 4096-second alerts and floating 32-year dates')


def test_atomic_alert_and_epoch_reuse():
    fw = Engine(); assert fw.call('log_eeprom_init')
    for i in range(30): fw.append(3, i, 100 + i, duration=4096)
    baseline = bytes(fw.eeprom)
    fw.writes = 0; fw.append(3, 999, 262144, duration=4096)
    writes = fw.writes
    for cut in range(writes + 1):
        trial = Engine(eeprom=baseline); assert trial.call('log_eeprom_init')
        trial.writes = 0; trial.cut = cut
        trial.cpu.mem_write(SCRATCH, struct.pack('<IIIHBx', 999, 262144, 60, 4096, 0))
        try: trial.call('log_eeprom_append', 3, 0, SCRATCH, YEAR)
        except PowerCut: pass
        trial.cut = None; trial.reset(); assert trial.call('log_eeprom_init')
        assert trial.call('log_eeprom_count', 3) == 30
        assert trial.call('log_eeprom_read', 3, 0, SCRATCH)
        index, count, maximum, duration, segment = struct.unpack('<IIIHBx', trial.cpu.mem_read(SCRATCH, 16))
        assert (index, count, maximum, duration, segment) in ((29, 129, 60, 4096, 0), (999, 262144, 60, 4096, 0))
        assert trial.eeprom[:316] == baseline[:316], 'Settings/reserve modified'
    fw = Engine(); fw.call('log_eeprom_init')
    fw.append(3, 10, 10, duration=2, epoch=0)
    fw.append(3, 20, 20, duration=2, epoch=63)
    fw.append(3, 30, 30, duration=2, epoch=64)
    fw.reset(); assert fw.call('log_eeprom_init')
    assert fw.call('log_eeprom_active_epoch', SCRATCH)
    assert int.from_bytes(fw.cpu.mem_read(SCRATCH, 2), 'little') == 64
    assert fw.call('log_eeprom_count', 3) == 2
    assert {p['epoch'] for p in fw.points(3, 60)} == {63, 64}
    print(f'PASS: alert-slot rollback at all {writes + 1} byte-write cuts, reboot and segment reuse')


def test_uptime_and_clock_epochs():
    fw = Engine(); fw.valid = False; fw.now = 0
    assert fw.call('log_writer_init')
    for second in range(1, 61): fw.call('log_writer_second', 1, 60, second, 0, 0)
    fw.reset(); fw.valid = True; fw.now = START
    assert fw.call('log_writer_init')
    points = fw.points(0, 60, 4)
    assert points[0]['time'] == 0 and not points[0]['calendar'] and points[0]['count'] == 60
    fw = Engine(); assert fw.call('log_writer_init')
    fw.now = START + 120; fw.call('test_feed_seconds', 120, 1, 60, START)
    fw.now = START + 100; fw.call('log_writer_second', 1, 60, 121, fw.now, 1)
    points = fw.points(0, 60, 4)
    assert points[0]['epoch'] == 0 and points[0]['count'] == 61
    assert len({(p['epoch'], p['time']) for p in points}) == len(points)
    fw.now = START + 3680; fw.call('log_writer_second', 1, 60, 122, fw.now, 1)
    points = fw.points(0, 60, 4)
    assert points[0]['epoch'] == 1 and any(p['epoch'] == 0 for p in points)
    print('PASS: unknown-time history stays uptime after RTC setup, minor sync extends a minute, large jumps separate epochs')


def test_segment_table_commit_and_corruption():
    fw = Engine(); fw.call('log_eeprom_init'); fw.append(3, 1, 10, duration=2)
    baseline = bytes(fw.eeprom)
    fw.writes = 0
    fw.cpu.mem_write(SCRATCH, struct.pack('<IIIHBx', 0, 20, 60, 2, 1))
    assert fw.call('log_eeprom_append', 3, 1, SCRATCH, YEAR + 1)
    writes = fw.writes
    for cut in range(writes + 1):
        trial = Engine(eeprom=baseline); assert trial.call('log_eeprom_init')
        trial.writes = 0; trial.cut = cut
        trial.cpu.mem_write(SCRATCH, struct.pack('<IIIHBx', 0, 20, 60, 2, 1))
        try: trial.call('log_eeprom_append', 3, 1, SCRATCH, YEAR + 1)
        except PowerCut: pass
        trial.cut = None; trial.reset(); assert trial.call('log_eeprom_init')
        count = trial.call('log_eeprom_count', 3)
        assert count in (1, 2)
        if count == 2:
            assert trial.call('log_eeprom_segment_year', 1, SCRATCH)
            assert bytes(trial.cpu.mem_read(SCRATCH, 1)) == bytes([YEAR + 1])
    broken = Engine(eeprom=baseline)
    broken.eeprom[1934 + 4] ^= 1
    assert not broken.call('log_eeprom_init'), 'Unexpected table CRC corruption must be reported'
    print(f'PASS: new-segment year table survives all {writes + 1} write cuts; unrelated CRC corruption is rejected')


def test_live_cascade_and_dates():
    fw = Engine(); assert fw.call('log_writer_init')
    fw.now = START + 3600; fw.uptime = 3600
    fw.call('test_feed_seconds', 3600, 1, 60, START)
    assert fw.call('log_eeprom_count', 0) == 60
    assert fw.call('log_eeprom_count', 1) == 1
    minutes = fw.points(0, 60, 3)
    hours = fw.points(1, 3600, 3)
    assert minutes[0]['live'] and minutes[0]['covered'] == 0
    assert minutes[1]['count'] == 60 and not minutes[1]['partial']
    assert hours[0]['live'] and hours[0]['covered'] == 0
    assert hours[1]['count'] == 3600 and hours[1]['covered'] == 3600 and not hours[1]['partial']
    # A reset loses the open minute and cannot turn a gap into a complete hour.
    fw.reset(); fw.now = START + 3630; fw.uptime = 0; assert fw.call('log_writer_init')
    fw.now = START + 3660; fw.call('test_feed_seconds', 30, 1, 60, START + 3630)
    assert fw.call('log_eeprom_count', 0) == 60
    dated = fw.points(0, 60, 4, START + 3599)
    assert dated[0]['time'] == START + 3540 and not dated[0]['live']
    print('PASS: full-minute/hour cascade, live overlay, partial restart and near-date bound')


def test_shared_aggregation_and_migration():
    fw = Engine(); assert fw.call('log_writer_init')
    minute_index = (START % (365 * 86400)) // 60  # use the shared converter below
    assert fw.call('log_record_location', 0, START, 1, SCRATCH + 128, SCRATCH + 132)
    minute_index = int.from_bytes(fw.cpu.mem_read(SCRATCH + 128, 4), 'little')
    for i in range(62): fw.append(0, minute_index + i, i + 1)
    for i in range(24): fw.append(1, minute_index // 60 + i, 3600)
    fw.now = START + 86400
    original = fw.points(0, 60, 70)
    expected_day = fw.points(2, 86400, 2)[0]
    assert fw.call('log_writer_change_memory', 1)
    fw.mode = fw.source = 1; fw.call('log_writer_memory_committed', 1)
    assert fw.points(0, 60, 70) == original
    assert fw.points(2, 86400, 2)[0] == expected_day
    assert expected_day['count'] == 84630, 'Younger complete minutes must take precedence over stored hours'
    assert fw.call('log_writer_change_memory', 0)
    fw.mode = fw.source = 0; fw.call('log_writer_memory_committed', 0)
    assert fw.points(0, 60, 70) == original
    assert fw.call('log_eeprom_count', 2) >= 1
    before = bytes(fw.eeprom), bytes(fw.nor)
    assert fw.call('log_writer_change_memory', 2)
    fw.mode = 2; fw.call('log_writer_memory_committed', 2)
    fw.call('log_writer_second', 10, 600, 1, fw.now + 1, 1)
    assert (bytes(fw.eeprom), bytes(fw.nor)) == before
    assert fw.points(0, 60, 70) == original
    assert fw.minimum_sp - fw.symbols['_ebss'] > 560, 'Insufficient headroom for main/protocol frames'
    print('PASS: identical canonical records across EEPROM/SPI round trip, daily aggregation, OFF and stack headroom')


def test_palette_dma():
    fw = Engine()
    colors = (0x0000, 0xffff, 0x07e0, 0xf800, 0xffe0, 0xbdf7)
    expected = [colors[i % len(colors)] for i in range(1600)]
    for i, color in enumerate(expected): fw.call('lcd_glyph_put', i, color)
    fw.set('lcd_text_stream_offset', 0, 2); fw.set('lcd_text_stream_total', 1600, 2)
    fw.set('lcd_text_stream_advance', 1)
    actual = []
    for i in range(10):
        fw.call('lcd_text_stream_next')
        actual.extend(struct.unpack('<160H', fw.cpu.mem_read(fw.symbols['lcd_text_dma_pixels'], 320)))
    assert actual == expected
    assert fw.get('lcd_text_stream_offset', 2) == 1600
    fw.set('lcd_text_stream_offset', 0, 2); fw.set('lcd_text_glyph_index', 4)
    fw.set('lcd_text_row_commands_queued', 1); fw.set('lcd_spi_ready', 1)
    fw.call('lcd_text_stream_next')
    fw.cpu.mem_write(0x40020000, struct.pack('<I', 1 << 17))
    for _ in range(9):
        fw.call('lcd_spi_pump')
        assert fw.get('lcd_text_glyph_index') == 4
    fw.call('lcd_spi_pump')
    assert fw.get('lcd_text_glyph_index') == 5 and not fw.get('lcd_dma_active')
    print('PASS: lossless RGB565 palette and all ten DMA chunks (no truncated glyph rows)')


def cobs_encode(data):
    out = bytearray([0]); code_pos = 0; code = 1
    for byte in data:
        if byte == 0:
            out[code_pos] = code; code_pos = len(out); out.append(0); code = 1
        else:
            out.append(byte); code += 1
            if code == 255:
                out[code_pos] = code; code_pos = len(out); out.append(0); code = 1
    out[code_pos] = code
    return bytes(out) + b'\x00'


def cobs_decode(data):
    out = bytearray(); at = 0
    while at < len(data):
        code = data[at]; at += 1
        out.extend(data[at:at + code - 1]); at += code - 1
        if code != 255 and at < len(data): out.append(0)
    return bytes(out)


def request(fw, opcode, payload):
    raw = struct.pack('<BBHHH', 1, 1, 7, opcode, len(payload)) + payload
    wire = cobs_encode(raw + struct.pack('<H', binascii.crc_hqx(raw, 0xffff)))
    assert len(wire) < 64
    fw.cpu.mem_write(fw.symbols['rx_ring'], wire)
    fw.set('rx_head', len(wire)); fw.set('rx_tail', 0); fw.set('encoded_length', 0)
    fw.tx.clear(); fw.call('protocol_v1_service')
    response = cobs_decode(bytes(fw.tx[:-1]))
    assert fw.tx[-1] == 0 and binascii.crc_hqx(response[:-2], 0xffff) == int.from_bytes(response[-2:], 'little')
    return response[8:-2]


def test_protocol_and_graph_parity():
    fw = Engine(); assert fw.call('log_writer_init')
    fw.now = START + 120; fw.uptime = 120
    fw.call('test_feed_seconds', 120, 1, 60, START)
    expected = fw.points(0, 60, 2)
    response = request(fw, 6, struct.pack('<BBIIHHI', 0, 2, 60, 0xffffffff, 0xffff, 0, 0))
    assert len(response) == 62 and response[:3] == bytes([0, 0, 2])
    for i, point in enumerate(expected):
        timestamp, cnt, avg, maximum, covered, epoch, flags, identity = struct.unpack('<5IHBH', response[12 + i * 25:37 + i * 25])
        assert (timestamp, cnt, avg, maximum, covered, epoch) == tuple(point[k] for k in ('time', 'count', 'avg', 'max', 'covered', 'epoch'))
        assert flags == point['available'] | point['partial'] << 1 | point['live'] << 2 | point['calendar'] << 3
    fw.set('lcd_graph_log_type', 1); fw.set('lcd_graph_selected', 0); fw.set('lcd_text_glyph_index', 0)
    assert fw.call('lcd_graph_prepare_glyph')
    columns = bytes(fw.cpu.mem_read(fw.symbols['lcd_graph_column_y'], 160))
    assert columns[53] & 127 == 79, 'First graph column must be the current zero-length point'
    assert columns[54] & 127 < 79, 'Saved nonzero minute disappeared from graph'
    info = request(fw, 5, b'')
    assert len(info) == 30 and info[26] == 2 and int.from_bytes(info[2:4], 'little') == 2
    print('PASS: canonical protocol rows exactly match reader points and graph columns; live log counts')


def test_spi_rollover_and_torn_records():
    fw = Engine(mode=1); assert fw.call('log_writer_init')
    assert fw.call('log_record_location', 0, START, 1, SCRATCH + 128, SCRATCH + 132)
    base = int.from_bytes(fw.cpu.mem_read(SCRATCH + 128, 4), 'little')
    for i in range(3 * 369): fw.append(0, base + i, 60)
    fw.now = START + (3 * 369 + 1) * 60
    fw.call('test_feed_seconds', 60, 1, 60, START + 3 * 369 * 60)
    fw.cpu.mem_write(SCRATCH + 128, b'\x01')
    assert fw.call('spi_log_query_read', SCRATCH + 128, 1, 0, START, SCRATCH + 64, SCRATCH + 132)
    assert struct.unpack('<IIIHBx', fw.cpu.mem_read(SCRATCH + 64, 16))[1] == 3600
    assert fw.call('spi_log_count', 1) == 7, 'Hours crossing sector edges must be promoted before erase'
    assert fw.call('spi_log_count', 0) == 739
    trial = Engine(mode=1); assert trial.call('log_writer_init'); trial.append(0, base, 123)
    baseline = bytes(trial.nor)
    for cut in range(12):
        item = Engine(mode=1, nor=baseline); assert item.call('log_writer_init')
        item.cpu.mem_write(SCRATCH, struct.pack('<IIIHBx', base + 1, 456, 60, 0, 0))
        item.spi_cut = cut; item.spi_writes = 0
        try: item.call('spi_log_append', 0, 0, SCRATCH)
        except PowerCut: pass
        item.spi_cut = None; item.reset(); assert item.call('log_writer_init')
        points = item.points(0, 60, 3)
        assert [p['count'] for p in points] in ([123], [456, 123])
    print('PASS: SPI ring rollover, cross-sector hour cascade, and every torn record byte/commit position')


def test_alert_identity_and_checkpoint_map():
    for mode in (0, 1):
        fw = Engine(mode=mode); assert fw.call('log_writer_init')
        assert fw.call('log_record_location', 3, START, 1, SCRATCH + 128, SCRATCH + 132)
        base = int.from_bytes(fw.cpu.mem_read(SCRATCH + 128, 4), 'little')
        fw.append(3, base, 10, duration=2); fw.append(3, base, 20, duration=3)
        events = fw.points(3, 60, 4)
        assert [p['count'] for p in events] == [20, 10] and events[0]['id'] != events[1]['id']
        fw.level = 3; fw.now = START + 1
        fw.call('log_writer_second', 2, 90, 1, START + 1, 1)
        events = fw.points(3, 60, 4)
        assert len(events) == 3 and events[0]['live'] and [p['count'] for p in events[1:]] == [20, 10]
        fw.level = 2; fw.now = START + 2
        fw.call('log_writer_second', 2, 30, 2, START + 2, 1)
        events = fw.points(3, 60, 4)
        assert [p['count'] for p in events] == [4, 20, 10] and not events[0]['live']
    fw = Engine(); before = bytes(fw.eeprom)
    fw.call('daily_dose_init', 9700, 0); fw.call('daily_dose_set_total', 1234)
    fw.call('daily_dose_add', 100); assert fw.call('daily_dose_checkpoint')
    assert fw.eeprom[:316] == before[:316] and fw.eeprom[348:] == before[348:]
    fw.reset(); fw.call('daily_dose_init', 9700, 0)
    assert fw.call('daily_dose_count') == 100 and fw.call('daily_dose_total') == 1234
    fw.call('daily_dose_set_day', 9701, 0)
    assert fw.call('daily_dose_count') == 0 and fw.call('daily_dose_total') == 1234
    legacy = Engine()
    day = bytearray(16); day[0] = 7; day[1:3] = struct.pack('<H', 9700)
    day[3:7] = struct.pack('<I', 55); day[8:10] = struct.pack('<H', binascii.crc_hqx(day[:8], 0xffff))
    day[14:16] = bytes([0xd2, 0x2d]); legacy.eeprom[172:188] = day
    total = bytearray([9]) + struct.pack('<I', 12345)
    total += struct.pack('<H', binascii.crc_hqx(total, 0xffff)) + bytes([0xc7, 0x38])
    for address, value in zip((10,11,12,13,14,15,30,31,62), total): legacy.eeprom[address] = value
    old_bytes = bytes(legacy.eeprom[:316])
    legacy.call('daily_dose_init', 9700, 0)
    assert legacy.call('daily_dose_count') == 55 and legacy.call('daily_dose_total') == 12345
    assert legacy.call('daily_dose_checkpoint')
    assert bytes(legacy.eeprom[:316]) == old_bytes
    legacy.reset(); legacy.call('daily_dose_init', 9700, 0)
    assert legacy.call('daily_dose_count') == 55 and legacy.call('daily_dose_total') == 12345
    print('PASS: distinct same-minute alerts, live/closed event identity, and non-overlapping DAY/TOTAL checkpoints')


def main():
    test_codec_and_time()
    test_atomic_alert_and_epoch_reuse()
    test_uptime_and_clock_epochs()
    test_segment_table_commit_and_corruption()
    test_live_cascade_and_dates()
    test_shared_aggregation_and_migration()
    test_palette_dma()
    test_protocol_and_graph_parity()
    test_spi_rollover_and_torn_records()
    test_alert_identity_and_checkpoint_map()


if __name__ == '__main__':
    main()
