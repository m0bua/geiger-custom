# Storage test images

- `spi-logs-fake-1mb.bin` — 1 MiB SPI NOR image with valid firmware-format logs: 120 minutes, 72 hours, 10 days, and 4 alerts. The dates span 2026-10-20 through 2026-10-29. The remaining SPI space is erased (`0xFF`). The records were written and reboot-scanned by the compiled firmware log code.
- `eeprom-device-dump-2026-10-29_09-26-52.bin` — unchanged 2 KiB EEPROM dump from `tmp/eeprom-2026-10-29_09-26-52.bin`.
- `eeprom-test-spi-enabled.bin` — test copy of that EEPROM dump, preserving its settings and data while selecting SPI log storage and setting the SPI layout marker (`D4 2B`). Use this together with the fake SPI image so startup preserves the synthetic logs instead of formatting SPI.

To regenerate the SPI image, run `python tools/build_firmware.py --test-support` followed by `python tools/generate_fake_spi_fixture.py`.
