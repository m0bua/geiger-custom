"""Exercise SPI log page boundaries, partial coverage, recovery and read faults.

Run after: python tools/build_firmware.py --test-support
"""
import binascii
import struct
import sys

sys.dont_write_bytecode = True

from test_log_engine import Engine, PowerCut, SCRATCH, START, YEAR


def append(fw, kind, period, coverage, duration=0):
    record = struct.pack('<IIIHBxI', period, 20, 120, duration, 0, coverage)
    fw.cpu.mem_write(SCRATCH, record)
    if fw.call('spi_log_prepare', kind, 0, YEAR, SCRATCH + 128) != 0:
        return False
    return bool(fw.call('spi_log_append', kind, 0, SCRATCH))


def engine(nor=None):
    fw = Engine(mode=1, nor=nor)
    assert fw.call('log_writer_init')
    return fw


def decoded_coverage(fw, kind, slot=0):
    base = (0, 16384, None, 32768)[kind] + 32
    size = 11 if kind == 3 else 9
    slot_size = size + 3
    fw.cpu.mem_write(SCRATCH + 256, bytes(fw.nor[base + slot * slot_size:base + slot * slot_size + size]))
    assert fw.call('log_record_decode', kind, SCRATCH + 256, size, SCRATCH + 288)
    return int.from_bytes(fw.cpu.mem_read(SCRATCH + 304, 4), 'little')


def test_page_boundaries_and_restart():
    for kind, coverage, duration in ((0, 60, 0), (1, 3600, 0), (3, 0, 10)):
        fw = engine()
        assert fw.call('spi_log_count', kind) == 0  # Exercise the cached counter.
        for i in range(80):
            assert append(fw, kind, 100 + i, coverage, duration), (kind, i)
        assert fw.call('spi_log_count', kind) == 80
        fw.reset()
        assert fw.call('log_writer_init')
        assert fw.call('spi_log_count', kind) == 80
        base = (0, 16384, None, 32768)[kind] + 32
        data_size = 11 if kind == 3 else 9
        for i in range(80):
            slot = fw.nor[base + i * (data_size + 3):base + (i + 1) * (data_size + 3)]
            context = struct.pack('<BBBHIH', kind, 0, YEAR, 0, 0, i)
            crc = binascii.crc_hqx(slot[:data_size], binascii.crc_hqx(context, 0xffff))
            assert slot[data_size + 2] == 0xA5 and int.from_bytes(slot[data_size:data_size + 2], 'little') == crc
            fw.cpu.mem_write(SCRATCH + 128, bytes(slot[:data_size]))
            assert fw.call('log_record_decode', kind, SCRATCH + 128, data_size, SCRATCH + 256), (kind, i)
            period, count = struct.unpack('<II', fw.cpu.mem_read(SCRATCH + 256, 8))
            assert (period, count) == (100 + i, 20), (kind, i, period, count)
        assert not fw.call('log_writer_fault')
    print('PASS: 80 minute, hour and alert records cross SPI pages and survive restart')


def test_rounded_coverage():
    for kind, coverage, duration, expected in (
        (0, 59, 0, 60), (0, 57, 0, 57), (0, 1, 0, 1),
        (1, 3300, 0, 3291), (1, 3600, 0, 3600),
        (3, 0xA5A5A5A5, 10, 0),
    ):
        fw = engine()
        assert fw.call('spi_log_count', kind) == 0
        assert append(fw, kind, 100, coverage, duration), (kind, coverage)
        assert decoded_coverage(fw, kind) == expected
        assert fw.call('spi_log_count', kind) == 1
        assert not fw.call('log_writer_fault')
        fw.reset()
        assert fw.call('log_writer_init')
        assert fw.call('spi_log_count', kind) == 1
        assert append(fw, kind, 101, coverage, duration)
        assert fw.call('spi_log_count', kind) == 2
    fw = engine()
    fw.call('test_feed_seconds', 19 * 60, 1, 60, START)
    assert fw.call('spi_log_count', 0) == 19
    assert not fw.call('log_writer_fault')
    print('PASS: 59-second minute, partial hour, alert and live writer count correctly')


def test_sector_transition():
    fw = engine()
    for i in range(340):
        assert append(fw, 0, 100 + i, 60), i
    assert fw.call('spi_log_count', 0) == 340
    fw.reset()
    assert fw.call('log_writer_init')
    assert fw.call('spi_log_count', 0) == 340
    assert append(fw, 0, 440, 60)
    assert fw.call('spi_log_count', 0) == 341
    assert not fw.call('log_writer_fault')
    print('PASS: sector transition and subsequent append survive restart')


def test_power_cut_on_split_record():
    initial = engine()
    for i in range(18):
        assert append(initial, 0, 100 + i, 60)
    snapshot = bytes(initial.nor)
    # The 19th record spans two pages: 11 payload bytes plus the commit byte.
    for cut in range(13):
        fw = engine(snapshot)
        fw.spi_cut, fw.spi_writes = cut, 0
        try:
            append(fw, 0, 118, 60)
        except PowerCut:
            pass
        fw.spi_cut = None
        fw.reset()
        assert fw.call('log_writer_init')
        before = fw.call('spi_log_count', 0)
        assert before in (18, 19), (cut, before)
        assert append(fw, 0, 119, 60), cut
        assert fw.call('spi_log_count', 0) == before + 1, cut
        assert not fw.call('log_writer_fault')
    print('PASS: every power-cut position on a split record keeps committed records')


class ReadFaultEngine(Engine):
    def __init__(self, nor=None):
        self.fail_address = None
        super().__init__(mode=1, nor=nor)

    def read_spi(self, args):
        address, _, length = args[:3]
        if self.fail_address is not None and address <= self.fail_address < address + length:
            self.fail_address = None
            return 0
        return super().read_spi(args)


class VerifyFaultEngine(Engine):
    def read_spi(self, args):
        address, ptr, length = args[:3]
        result = super().read_spi(args)
        # Corrupt only the verification response, after the commit marker.
        if address == 32 and length == 12 and self.nor[43] == 0xA5:
            data = bytearray(self.cpu.mem_read(ptr, length))
            data[0] ^= 1
            self.cpu.mem_write(ptr, bytes(data))
        return result


def test_read_faults_and_corruption():
    source = engine()
    assert append(source, 0, 100, 60)
    snapshot = bytes(source.nor)

    # An SPI transfer failure during recovery must not erase or hide it as empty.
    fw = ReadFaultEngine(snapshot)
    fw.fail_address = 0
    assert not fw.call('log_writer_init')
    assert fw.call('log_writer_fault')
    assert bytes(fw.nor) == snapshot

    # A failed count must not cache the failed result as an empty log.
    fw = ReadFaultEngine(snapshot)
    assert fw.call('log_writer_init')
    fw.fail_address = 32
    assert fw.call('spi_log_count', 0) == 0
    assert fw.call('log_writer_fault')
    assert fw.call('spi_log_count', 0) == 1

    # A read failure during normal history access becomes visible in STORE.
    fw = ReadFaultEngine(snapshot)
    assert fw.call('log_writer_init')
    fw.call('log_history_begin', SCRATCH + 256)
    fw.fail_address = 32
    assert not fw.call('log_history_next', 0, 60, SCRATCH + 256, 0, SCRATCH + 320)
    assert fw.call('log_writer_fault')
    assert fw.call('log_history_next', 0, 60, SCRATCH + 256, 0, SCRATCH + 320)
    assert bytes(fw.nor) == snapshot

    # A failed record read during recovery is also an initialization error.
    fw = ReadFaultEngine(snapshot)
    fw.fail_address = 32
    assert not fw.call('log_writer_init')
    assert fw.call('log_writer_fault')
    assert bytes(fw.nor) == snapshot

    # A failure while locating a free sector must stop before any write/erase.
    fw = ReadFaultEngine()
    assert fw.call('log_writer_init')
    before = bytes(fw.nor)
    fw.fail_address = 4096
    assert fw.call('spi_log_prepare', 0, 0, YEAR, SCRATCH + 128) == 2
    assert fw.call('log_writer_fault')
    assert bytes(fw.nor) == before

    # A committed record with a bad CRC must be reported during recovery.
    damaged = bytearray(snapshot)
    damaged[32] ^= 1
    fw = ReadFaultEngine(damaged)
    assert not fw.call('log_writer_init')
    assert fw.call('log_writer_fault')
    assert bytes(fw.nor) == bytes(damaged)

    damaged = bytearray(snapshot)
    damaged[14] ^= 1
    fw = ReadFaultEngine(damaged)
    assert not fw.call('log_writer_init')
    assert fw.call('log_writer_fault')
    assert bytes(fw.nor) == bytes(damaged)

    # A successful SPI transfer with incorrect verification bytes must fail.
    fw = VerifyFaultEngine(mode=1)
    assert fw.call('log_writer_init')
    assert not append(fw, 0, 101, 59)
    print('PASS: SPI read failures and corrupt committed data report STORE ERR without erasing data')


if __name__ == '__main__':
    test_page_boundaries_and_restart()
    test_rounded_coverage()
    test_sector_transition()
    test_power_cut_on_split_record()
    test_read_faults_and_corruption()
