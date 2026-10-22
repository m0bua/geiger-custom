# Geiger-2 Console

Small browser-only client for the firmware protocol v1 (`HELLO` and `GET_STATUS`). It has no build step, dependencies, or native components.

## Run

For the most reliable serial access, serve this directory from `http://localhost` or HTTPS in a browser with Web Serial support (tested in Chrome/Edge and Firefox). From the repository root, run a local static server and open `http://localhost:8000/app/`. The page uses a classic deferred script (no ES-module import), so the UI itself can also be opened as `file://`; Web Serial may still require localhost/HTTPS depending on browser security policy.

The firmware currently speaks USART1 at 115200 8N1 over the board's USB-UART bridge. Select its serial port in the browser prompt. The session line displays the USB VID:PID to help identify the bridge. Opening the port may affect DTR/RTS on some bridges and can reset the device; the app waits for startup and retries `HELLO` automatically.

The Overview uses the device's UNIT and prefix settings when available; otherwise it falls back to µSv/h and µSv. Rate is calculated from CPM using the firmware coefficient. The page also shows today's accumulated dose and a rolling chart of the latest 60 rate readings. The Debug tab contains uptime, raw ADC values, and device flags. The Settings tab reads and applies supported persistent device options; RATE ROW 2 and action items are not exposed. Logs and firmware transfer are not implemented yet.
