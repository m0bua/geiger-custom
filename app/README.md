# Geiger-2 Console

Small browser-only client for firmware protocol v1. It has no build step, dependencies, or native components.

## Run

For the most reliable serial access, serve this directory from `http://localhost` or HTTPS in a browser with Web Serial support (tested in Chrome/Edge and Firefox). From the repository root, run a local static server and open `http://localhost:8000/app/`. The page uses a classic deferred script (no ES-module import), so the UI itself can also be opened as `file://`; Web Serial may still require localhost/HTTPS depending on browser security policy.

The firmware currently speaks USART1 at 115200 8N1 over the board's USB-UART bridge. Select its serial port in the browser prompt. The session line displays the USB VID:PID to help identify the bridge. Opening the port may affect DTR/RTS on some bridges and can reset the device; the app waits for startup and retries `HELLO` automatically.

The Overview and Logs use the device's UNIT and prefix settings. The rate chart follows the selected rate unit. Logs is placed before Settings and Debug. Debug mirrors the device's four pages: uptime/CNT/RTC/clock, memory, physical log counts, and voltage with raw ADC values. Settings reads and applies supported persistent device options; RATE ROW 2 and action items are not exposed.

Logs reads record counts after connection, then fetches records on demand in pages of three. DAYS is always available; MINUTES, HOURS, and ALERTS appear when USE-SPI is enabled. Downloaded pages stay in browser memory; use **Load more** to retrieve the next page. Firmware update remains a placeholder.

Protocol operations:

- `GET_STATUS` (2): empty request; response retains the original 25-byte status and appends `count_total:u32` for the device-style debug CNT value. Old clients can ignore the appended bytes.
- `GET_LOG_INFO` (5): empty request; response begins with `status:u8 | use_spi:u8 | minute_count:u16 | hour_count:u16 | day_count:u16 | alert_record_count:u16 | calendar_valid:u8 | calendar_seconds:u32`, followed by debug details `spi_size_mb:u8 | spi_ready:u8 | jedec[3] | rtc_status:u8 | clock_source:u8 | calendar_uptime:u32`.
- `GET_LOG_PAGE` (6): request is `type:u8 | offset:u16 | limit:u8`; types are DAYS=0, MINUTES=1, HOURS=2, ALERTS=3. Limit is 1–3. Response is `status:u8 | type:u8 | offset:u16 | total:u16 | count:u8 | records[count]`.
- Aggregate record: `period_start_seconds_since_2000:u32 | dose_pulses:u32 | minimum_cpm:u32 | maximum_cpm:u32`. The first entry is the current RAM aggregate, followed by persisted history newest-first.
- Alert record: `start_seconds_since_2000:u32 | duration_seconds:u32 | dose_pulses:u32 | maximum_cpm:u32`.
