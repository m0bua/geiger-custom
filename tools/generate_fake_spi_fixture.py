"""Create a 1 MiB SPI image with valid synthetic log records.

Build the firmware first:
    python tools/build_firmware.py
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

import test_log_engine  # noqa: E402

test_log_engine.BUILD = ROOT / ".pio/scratch/geiger2_manual"
Engine = test_log_engine.Engine
SCRATCH = test_log_engine.SCRATCH

CAPACITY = 1024 * 1024
NOW = datetime(2026, 10, 29, 9, 26, tzinfo=timezone.utc)


def period_index(moment: datetime, period_seconds: int) -> int:
    year_start = datetime(moment.year, 1, 1, tzinfo=timezone.utc)
    return int((moment - year_start).total_seconds()) // period_seconds


def append(firmware: Engine, kind: int, moment: datetime, count: int,
           maximum_cpm: int, coverage: int, duration: int = 0) -> None:
    period = 60 if kind == 3 else (60, 3600, 86400)[kind]
    record = struct.pack(
        "<IIIHBxI", period_index(moment, period), count, maximum_cpm,
        duration, 0, coverage,
    )
    firmware.cpu.mem_write(SCRATCH, record)
    year_descriptor = 0x20 | (moment.year & 0x1F)
    result = firmware.call("spi_log_prepare", kind, 0, year_descriptor,
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


def generate(output: Path, counts: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    firmware = Engine(mode=1, nor=b"\xff" * CAPACITY)
    if not firmware.call("log_writer_init"):
        raise RuntimeError("Could not initialize the SPI log layout")

    minute_count, hour_count, day_count, alert_count = counts
    minute_start = NOW.replace(second=0) - timedelta(minutes=minute_count)
    add_periods(firmware, 0,
                [minute_start + timedelta(minutes=i) for i in range(minute_count)], 60)

    hour_start = NOW.replace(minute=0, second=0) - timedelta(hours=hour_count)
    add_periods(firmware, 1,
                [hour_start + timedelta(hours=i) for i in range(hour_count)], 3600)

    day_start = NOW.replace(hour=0, minute=0, second=0) - timedelta(days=day_count - 1)
    add_periods(firmware, 2,
                [day_start + timedelta(days=i) for i in range(day_count)], 86400)

    for index in range(alert_count):
        moment = NOW - timedelta(hours=(alert_count - index) * 12)
        pulses = 36 + (index * 37) % 900
        maximum = min(262144, 63 + (index * 17) % 480)
        duration = 42 + (index * 53) % 1800
        append(firmware, 3, moment, pulses, maximum, 0, duration)

    expected_counts = counts
    for kind, expected in enumerate(expected_counts):
        actual = firmware.call("spi_log_count", kind)
        if actual != expected:
            raise RuntimeError(f"kind={kind}: expected {expected} records, got {actual}")

    firmware.reset()
    if not firmware.call("spi_log_init"):
        raise RuntimeError("Generated image did not pass the SPI log scan after reboot")
    actual_counts = tuple(firmware.call("spi_log_count", kind) for kind in range(4))
    if actual_counts != expected_counts:
        raise RuntimeError(f"Reboot scan returned unexpected counts: {actual_counts}")

    alerts = firmware.points(3, 60, limit=alert_count)
    if len(alerts) != alert_count or any(
        left["time"] <= right["time"] for left, right in zip(alerts, alerts[1:])
    ):
        raise RuntimeError("Alert history is incomplete or not in newest-first order")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(firmware.nor)

    print(f"Wrote {output.relative_to(ROOT)} ({output.stat().st_size:,} bytes)")
    print("Recovered log counts MIN/HOUR/DAY/ALERT:", *actual_counts)
    return actual_counts


def main() -> None:
    fixtures = ROOT / "tests/fixtures"
    generate(fixtures / "spi-logs-fake-1mb.bin", (120, 72, 10, 4))
    generate(fixtures / "spi-logs-load-500-each-1mb.bin", (500, 500, 500, 500))


if __name__ == "__main__":
    main()
