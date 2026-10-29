# Storage test images

- `spi-logs-fake-1mb.bin` — 1 MiB SPI NOR image with valid firmware-format logs: 120 minutes, 72 hours, 10 days, and 4 alerts. The dates span 2026-10-20 through 2026-10-29. The remaining SPI space is erased (`0xFF`). The records were written and reboot-scanned by the compiled firmware log code.
- `spi-logs-load-500-each-1mb.bin` — 1 MiB load-test image with 500 records in each stream: minutes, hours, days, and alerts. The daily records span 500 calendar days across the 2025/2026 year boundary; the alert records span about 250 days in chronological order. Each partition contains valid records written and reboot-scanned by the compiled firmware log code. Use it with `eeprom-test-spi-enabled.bin` to load the image on the device.
- `eeprom-device-dump-2026-10-29_09-26-52.bin` — unchanged 2 KiB EEPROM dump from `tmp/eeprom-2026-10-29_09-26-52.bin`.
- `eeprom-test-spi-enabled.bin` — test copy of that EEPROM dump, preserving its settings and data while selecting SPI log storage and setting the SPI layout marker (`D4 2B`). Use this together with the fake SPI image so startup preserves the synthetic logs instead of formatting SPI.

To regenerate both SPI images, run `python tools/build_firmware.py` followed by `python tools/generate_fake_spi_fixture.py`.

To check alert graph reads, five-record protocol pages for every log type, date filtering, and same-minute alerts across a sector boundary, run `python tools/test_spi_alert_paging.py` after building the firmware.
