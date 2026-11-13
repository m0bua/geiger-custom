"""Test NORMAL rate smoothing in the compiled Cortex-M0+ firmware.

python tools/build_firmware.py --test-support
python tools/test_rate_display.py
"""
import random
import struct

from test_log_engine import Engine, SCRATCH


def main():
    firmware = Engine()
    step = lambda current, target, elapsed=500: firmware.call('rate_display_step', current, target, elapsed // 100)
    for fast in (False, True):
        for screen in (False, True):
            for invisible in (False, True):
                expected = 100 if fast else 500 if screen else 5000 if invisible else 1000
                assert firmware.call('rate_display_interval', fast, screen, invisible) == expected
    def last_update(value):
        firmware.cpu.mem_write(SCRATCH, struct.pack('<II', value, value))

    def due(now, interval, pulse=False):
        return firmware.call('rate_display_ticks', SCRATCH, now, interval, pulse) != 0xffffffff

    def stored_time():
        return struct.unpack('<I', firmware.cpu.mem_read(SCRATCH, 4))[0]

    for interval in (500, 1000, 5000):
        last_update(0)
        assert not due(100, interval)
        assert due(200, interval, True), 'pulse must update immediately'
        assert stored_time() == 200
        assert not due(interval, interval), 'old deadline must be cancelled'
        assert not due(200 + interval - 1, interval)
        assert due(200 + interval, interval), 'idle update follows the latest pulse'
        assert due(201 + interval, interval, True), 'next pulse restarts countdown again'
    last_update(0)
    assert not due(50, 100, True), 'SEARCH/USB ignore pulse timer resets'
    assert stored_time() == 0
    assert due(100, 100)
    assert not due(150, 100, True)
    assert due(200, 100)
    last_update(0xfffffff0)
    assert not due(20, 100)
    assert due(84, 100), 'timer handles uint32 wrap'
    # Frequent events must not erase the filter's sub-100-ms elapsed time.
    last_update(0)
    current = 225
    for now in range(50, 1001, 50):
        elapsed = firmware.call('rate_display_ticks', SCRATCH, now, 5000, True)
        assert elapsed != 0xffffffff
        current = step(current, 150, elapsed * 100)
    assert current == step(225, 150, 1000), 'pulse updates retain the same decay speed'
    # Exercise the same stateful controller that main uses, including consuming
    # each pulse flag once and retaining fractional filter time between pulses.
    def reset_state(value=225):
        firmware.cpu.mem_write(SCRATCH, struct.pack('<IIIB3x', value, 0, 0, 0))

    def update(now, target, interval, pulse=False):
        firmware.cpu.mem_write(SCRATCH + 12, bytes([pulse]))
        result = firmware.call('rate_display_update', SCRATCH, now, target, interval)
        state = struct.unpack('<IIIB3x', firmware.cpu.mem_read(SCRATCH, 16))
        assert state[3] == 0, 'consume the pulse flag even between fast ticks'
        return result, state

    for interval in (500, 1000, 5000):
        reset_state()
        assert update(200, 150, interval, True)[0]
        assert not update(interval, 150, interval)[0]
        assert update(interval + 200, 150, interval)[0]
    reset_state()
    for now in range(50, 1001, 50):
        refreshed, state = update(now, 150, 5000, True)
        assert refreshed
    assert state[0] == step(225, 150, 1000)
    assert state[1:3] == (1000, 1000)
    reset_state()
    assert not update(50, 150, 100, True)[0]
    assert update(100, 150, 100)[0]
    reset_state()
    assert update(100, 0xffffffff, 100, True)[1][0] == 225, 'keep SEARCH estimate separate'
    # About 0.15 -> 0.10 uSv/h. The drop must be gradual, monotonic and
    # converge exactly instead of stalling at low integer CPM.
    current, target = 225, 150
    fall = [current]
    for _ in range(40):
        updated = step(current, target)
        assert target <= updated <= current
        assert updated == target or updated < current
        current = updated
        fall.append(current)
    assert target < fall[2] < fall[1] < fall[0]
    assert fall[10] > target and fall[-1] == target
    assert step(150, 225) - 150 > 225 - step(225, 150), 'rise faster than fall'
    assert step(225, 150, 0) == 225
    for start, target in [(225, 150), (150, 225), (2621440, 0), (0, 2621440)]:
        # Equal elapsed time produces exactly the same result in each mode.
        results = []
        for interval in (100, 500, 1000, 5000):
            current = start
            for _ in range(10000 // interval):
                current = step(current, target, interval)
            results.append(current)
        assert len(set(results)) == 1, results
    assert step(2621440, 0, 0xffffffff) == 0, 'long pauses stop at the target'
    # Nonzero endpoints, zero, one-unit differences and full uint32 bounds.
    cases = [(0, 0), (225, 225), (0, 1), (1, 0), (225, 224),
             (224, 225), (0xffffffff, 0), (0, 0xffffffff)]
    rng = random.Random(20261113)
    cases += [(rng.randrange(2**32), rng.randrange(2**32)) for _ in range(100)]
    for current, target in cases:
        result = step(current, target)
        assert min(current, target) <= result <= max(current, target)
        assert result != current or current == target
    for target in (0, 1, 149, 225, 2621440):
        current = 2621440
        for _ in range(200):
            current = step(current, target)
        assert current == target
    print('PASS: ARM priorities, pulse timer resets, fast-mode cadence, timer wrap, time-independent smoothing and bounds')
    print('0.15 -> 0.10 uSv/h, one-second samples:',
          ' '.join(f'{value * 0.000666:.3f}' for value in fall[:21:2]))


if __name__ == '__main__':
    main()
