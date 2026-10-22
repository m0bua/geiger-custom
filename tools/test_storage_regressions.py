"""Boundary regressions against the built Cortex-M0+ firmware (Unicorn)."""
from test_graph_logs import Firmware
from unicorn import UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_PC


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ (7 if crc & 128 else 0)) & 255
    return crc


def test_day_checkpoint():
    for spi in (0, 1):
        fw = Firmware()
        fw.call('log_storage_init', spi)
        fw.call('log_storage_calendar_day', 9700, 1)
        for _ in range(3):
            fw.minute()
        for _ in range(17):
            fw.call('log_storage_second', 1)
        fw.call('log_storage_checkpoint', 9700 * 86400 + 197, 1, 1, 0)
        writes = fw.eeprom_checkpoint_writes
        fw.reset()
        fw.call('log_storage_init', spi)
        assert fw.eeprom_checkpoint_writes == writes, 'Normal boot rewrote DAY'
        fw.call('log_storage_calendar_day', 9700, 1)
        fw.call('log_storage_restore_live', 9700 * 86400 + 197, 1)
        assert fw.call('log_storage_today_pulse_count') == 197, \
            'Lost or double-counted the open minute in DAY'
        assert fw.call('log_storage_graph_current_seconds', 2) == 197, \
            'DAY duration changed during reboot'
        fw.call('log_storage_second', 2)
        assert fw.call('log_storage_today_pulse_count') == 199
        assert fw.call('log_storage_graph_current_seconds', 2) == 198
    print('PASS: exact DAY dose/duration across reboot, SPI on/off, no boot writes')


def test_live_sector_rollover():
    fw = Firmware()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9700, 1)
    # 185 records fill a live sector. The following two records exercise both
    # the rollover and the formerly-restarted per-sector sequence.
    for value in range(1, 188):
        fw.set('hour_dose', value, 4)
        fw.call('log_storage_checkpoint', 9700 * 86400 + value * 10, 1, 1, 0)
    fw.reset()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9700, 1)
    fw.call('log_storage_restore_live', 9700 * 86400 + 1870, 1)
    assert fw.get('hour_dose', 4) == 187, 'Older sector snapshot won'
    # Tear the last record: the preceding valid slot must still be recoverable.
    base = 175 * 4096 + 16 + 22
    fw.nor[base + 20] ^= 1
    fw.reset()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_calendar_day', 9700, 1)
    fw.call('log_storage_restore_live', 9700 * 86400 + 1870, 1)
    assert fw.get('hour_dose', 4) == 186, 'Torn snapshot hid previous valid slot'
    print('PASS: newest SPI snapshot across sector rollover and torn writes')


def test_checkpoint_migration():
    fw = Firmware()
    old = bytearray(17)
    old[2] = 12
    old[3:5] = (9700).to_bytes(2, 'little')
    old[5:9] = (12345).to_bytes(4, 'little')
    old[9:12] = (21).to_bytes(3, 'little')
    old[12:15] = (47).to_bytes(3, 'little')
    old[15] = crc8(old[:15])
    old[16] = 0xD5
    fw.eeprom[1806:1823] = old
    fw.call('log_storage_init', 0)
    fw.call('log_storage_calendar_day', 9700, 1)
    assert fw.call('log_storage_today_pulse_count') == 12345
    assert fw.call('log_storage_graph_current_seconds', 2) == 0, \
        'Migration invented elapsed time absent from the old format'
    writes = fw.eeprom_checkpoint_writes
    assert writes == 21, 'D5 must migrate once into D6'
    fw.reset()
    fw.call('log_storage_init', 0)
    assert fw.eeprom_checkpoint_writes == writes, 'Migration repeated at boot'
    assert fw.call('log_storage_today_pulse_count') == 12345
    print('PASS: old DAY checkpoint migrates once without changing dose')


def test_counter_and_font():
    fw = Firmware()
    fw.set('board_hv_enabled', 1)
    fw.set('detector_pulse_count', 999999, 4)
    fw.write32(0x40010414, 1 << 6)
    fw.call('EXTI4_15_IRQHandler')
    assert fw.call('board_detector_count') == 1000000, 'Counter stops at UI limit'
    fw.set('detector_pulse_count', 0xFFFFFFFF, 4)
    fw.call('EXTI4_15_IRQHandler')
    assert fw.call('board_detector_count') == 0xFFFFFFFF, 'Counter wrapped'
    reads = []
    end = fw.symbols['micro_rows'] + 8
    pixel_start = fw.symbols['font_small_pixel']
    pixel_end = min(value for value in fw.symbols.values() if value > pixel_start)
    def watch_bitmap(cpu, access, address, size, value, user):
        # small_glyphs can be placed immediately after micro_rows. Its normal
        # lookup reads that address too; only bitmap accesses count here.
        if pixel_start <= cpu.reg_read(UC_ARM_REG_PC) < pixel_end:
            reads.append(address)
    fw.cpu.hook_add(UC_HOOK_MEM_READ,
                    watch_bitmap,
                    begin=end, end=end)
    for x in range(7):
        for y in range(10):
            fw.call('font_small_pixel', 0xB5, x, y)
    assert not reads, 'Micro glyph accessed memory outside its bitmap'
    print('PASS: pulse count beyond 999999 and micro glyph bitmap bounds')


def test_clear_and_settings():
    fw = Firmware()
    fw.call('log_storage_init', 0)
    fw.call('log_storage_calendar_day', 9700, 1)
    fw.minute()
    assert fw.call('log_storage_clear', 2)  # DAY
    fw.call('log_storage_second', 7)
    fw.call('log_storage_checkpoint', 9700 * 86400 + 600, 1, 0, 0)
    fw.reset()
    fw.call('log_storage_init', 0)
    fw.call('log_storage_calendar_day', 9700, 1)
    assert fw.call('log_storage_today_pulse_count') == 7, \
        'DAY clear lost the active date or resurrected old dose'
    assert fw.log_counts()[2] == 0
    assert fw.call('settings_value_write', 120, 0xB6, 0x6D, 4)
    fw.call('settings_clicks_load')
    address = fw.symbols['lcd_settings_value_index'] + 1
    assert fw.cpu.mem_read(address, 1)[0] == 4
    fw.eeprom[122] ^= 1
    fw.cpu.mem_write(address, b'\0')
    fw.call('settings_clicks_load')
    assert fw.cpu.mem_read(address, 1)[0] == 0, 'Invalid setting CRC accepted'
    # The historical EEPROM ordering maps stored MODERN=1 to menu MODERN=2.
    assert fw.call('settings_value_write', 124, 0xB8, 0x73, 1)
    fw.call('settings_click_tone_load')
    assert fw.cpu.mem_read(fw.symbols['lcd_settings_value_index'] + 20, 1)[0] == 2
    print('PASS: DAY clear continues saving; shared settings CRC and TONE mapping')


def test_alert_sector_rollover():
    fw = Firmware()
    fw.call('log_storage_init', 1)
    fw.call('log_storage_alert_sample', 61, 1, 1, 1)
    for i in range(186):
        fw.set('alert_seconds_since_save', 59, 4)
        fw.call('log_storage_alert_sample', 61, 1, i + 2, 1)
    fw.reset()
    fw.call('log_storage_init', 1)
    # The reboot appends an interrupted record after the two records in the
    # second alert sector. It must summarize the newest episode snapshot.
    base = 118 * 4096 + 16 + 2 * 22
    assert int.from_bytes(fw.nor[base + 12:base + 16], 'little') == 187
    assert fw.nor[base + 19] & 8 and not fw.nor[base + 19] & 4
    print('PASS: alert reboot recovery selects newest sector snapshot')


if __name__ == '__main__':
    test_day_checkpoint()
    test_live_sector_rollover()
    test_checkpoint_migration()
    test_counter_and_font()
    test_clear_and_settings()
    test_alert_sector_rollover()
