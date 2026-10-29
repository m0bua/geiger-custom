"""Create a 1 MiB SPI image with valid synthetic log records.

Build the ARM log test support first:
    python tools/build_firmware.py --test-support
Then run:
    python tools/generate_fake_spi_fixture.py
"""

from __future__ import annotations

from datetime import datetime, timedelta, timezone
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from test_log_engine import Engine, SCRATCH  # noqa: E402

CAPACITY = 1024 * 1024
EPOCH_START = datetime(2026, 1, 1, tzinfo=timezone.utc)
NOW = datetime(2026, 10, 29, 9, 26, tzinfo=timezone.utc)
YEAR_DESCRIPTOR = 0x20 | (2026 & 0x1F)


def period_index(moment: datetime, period_seconds: int) -> int:
    return int((moment - EPOCH_START).total_seconds()) // period_seconds


def append(firmware: Engine, kind: int, moment: datetime, count: int,
           maximum_cpm: int, coverage: int, duration: int = 0) -> None:
    period = 60 if kind == 3 else (60, 3600, 86400)[kind]
    record = struct.pack(
        "<IIIHBxI", period_index(moment, period), count, maximum_cpm,
        duration, 0, coverage,
    )
    firmware.cpu.mem_write(SCRATCH, record)
    result = firmware.call("spi_log_prepare", kind, 0, YEAR_DESCRIPTOR,
                           SCRATCH + 128)
    if result != 0:
        raise RuntimeError(f"SPI log prepare failed for kind={kind}: {result}")
    if not firmware.call("spi_log_append", kind, 0, SCRATCH):
        raise RuntimeError(f"SPI append failed for kind={kind} at {moment!s}")


def add_periods(firmware: Engine, kind: int, moments: list[datetime],
                period_seconds: int) -> None:
    for index, moment in enumerate(moments):
        average = 7 + (index * 7) % 14
        if index % 29 in (12, 13):
            average += 24
        coverage = period_seconds
        if kind == 0 and index % 23 == 7:
            coverage = 54
        elif kind == 0 and index % 31 == 11:
            coverage = 47
        elif kind == 1 and index % 19 == 3:
            coverage = 2780
        elif kind == 2 and moment.date() == NOW.date():
            coverage = 9 * 3600 + 26 * 60
        count = (average * coverage + 30) // 60
        maximum = min(262144, average + 3 + (index * 11) % 19)
        append(firmware, kind, moment, count, maximum, coverage)


def main() -> None:
    output = ROOT / "tests/fixtures/spi-logs-fake-1mb.bin"
    firmware = Engine(mode=1, nor=b"\xff" * CAPACITY)
    if not firmware.call("log_writer_init"):
        raise RuntimeError("Could not initialize the SPI log layout")

    minute_start = NOW.replace(second=0) - timedelta(minutes=120)
    add_periods(firmware, 0,
                [minute_start + timedelta(minutes=i) for i in range(120)], 60)

    hour_start = NOW.replace(minute=0, second=0) - timedelta(hours=72)
    add_periods(firmware, 1,
                [hour_start + timedelta(hours=i) for i in range(72)], 3600)

    day_start = datetime(2026, 10, 20, tzinfo=timezone.utc)
    add_periods(firmware, 2,
                [day_start + timedelta(days=i) for i in range(10)], 86400)

    alerts = (
        (datetime(2026, 10, 27, 11, 25, tzinfo=timezone.utc), 36, 63, 42),
        (datetime(2026, 10, 28, 2, 14, tzinfo=timezone.utc), 128, 118, 390),
        (datetime(2026, 10, 28, 16, 3, tzinfo=timezone.utc), 61, 82, 85),
        (datetime(2026, 10, 29, 8, 10, tzinfo=timezone.utc), 738, 280, 1390),
    )
    for moment, count, maximum, duration in alerts:
        append(firmware, 3, moment, count, maximum, 0, duration)

    expected_counts = (120, 72, 10, len(alerts))
    for kind, expected in enumerate(expected_counts):
        actual = firmware.call("spi_log_count", kind)
        if actual != expected:
            raise RuntimeError(f"kind={kind}: expected {expected} records, got {actual}")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(firmware.nor)

    firmware.reset()
    if not firmware.call("spi_log_init"):
        raise RuntimeError("Generated image did not pass the SPI log scan after reboot")
    actual_counts = tuple(firmware.call("spi_log_count", kind) for kind in range(4))
    if actual_counts != expected_counts:
        raise RuntimeError(f"Reboot scan returned unexpected counts: {actual_counts}")

    print(f"Wrote {output.relative_to(ROOT)} ({output.stat().st_size:,} bytes)")
    print("Recovered log counts MIN/HOUR/DAY/ALERT:", *actual_counts)


if __name__ == "__main__":
    main()
