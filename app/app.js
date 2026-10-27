const VERSION = 1;
const KIND_REQUEST = 1;
const KIND_RESPONSE = 2;
const OP_HELLO = 1;
const OP_GET_STATUS = 2;
const OP_GET_SETTINGS = 3;
const OP_SET_SETTING = 4;
const OP_GET_LOG_INFO = 5;
const OP_GET_LOG_PAGE = 6;
const OP_GET_SPI_DUMP_PAGE = 7;
const OP_SET_RTC = 8;
const OP_DEVICE_ACTION = 9;
const CAP_GET_SETTINGS = 1 << 1;
const CAP_SET_SETTING = 1 << 2;
const CAP_GET_SPI_DUMP = 1 << 5;
const CAP_SET_RTC = 1 << 6;
const CAP_DEVICE_ACTION = 1 << 7;
const MAX_PAYLOAD = 64;
const LOG_BATCH_SIZE = 5;
const LOG_INITIAL_TARGETS = { minutes: 60, hours: 24, days: 30, alerts: 5 };
const STM32_FLASH_BASE = 0x08000000;
const STM32_FLASH_SIZE = 64 * 1024;
const STM32_FLASH_PAGE_SIZE = 128;
const STM32_EEPROM_BASE = 0x08080000;
const STM32_EEPROM_SIZE = 2 * 1024;
const SPI_DUMP_CHUNK_SIZE = 58;
const STM32_DEVICE_ID = 0x0417;
const STM32_FLASH_PAGE_COUNT = STM32_FLASH_SIZE / STM32_FLASH_PAGE_SIZE;
const PROJECT_IMAGE_MARKER = Uint8Array.of(0x4d, 0x47, 0x32, 0x46, 0x57, 0x49, 0x44, 0x31); // MG2FWID1
// Exact 790-byte common block in the four supplied stock HEX variants.
const STOCK_FAMILY_SIGNATURE_OFFSET = 0x106;
const STOCK_FAMILY_SIGNATURE_LENGTH = 790;
const STOCK_FAMILY_SIGNATURE_SHA256 = '6dee8762ae06eb90f6400ed805221ea6d4cccf54ae69c9f7d04f7d65e0c401b0';
const KNOWN_STOCK_IMAGE_SHA256 = 'cee27644cccea4023118f97d1a990be3bf6acb44748d61c0811f4cee0ed53386';
// SHA-256 of the canonical 64 KiB flash image (gaps filled with 0xFF), not the source file.
// Maintain these lists for this JS protocol; clear/review them when the protocol changes.
const COMPATIBLE_PROJECT_IMAGE_SHA256 = new Set([
  'ca1447612d433d5dfdfcb7d2c7f14d9a4a98fde337fbb9160b896aa1b77fbe81',
]);
// Known project builds with a non-breaking protocol/settings difference (for example,
// a changed settings list) while core operations remain usable. Add hashes only after review.
const PARTIALLY_COMPATIBLE_PROJECT_IMAGE_SHA256 = new Set([]);

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function checkFirmwareStopRequested() {
  if (firmwareStopRequested) throw new Error('Update stopped by user.');
}

class Stm32Bootloader {
  constructor(port) {
    this.port = port;
    this.reader = port.readable.getReader();
    this.writer = port.writable.getWriter();
    this.buffer = [];
  }

  async write(bytes) {
    await this.writer.write(Uint8Array.from(bytes));
  }

  async readBytes(length, timeoutMs = 3000) {
    while (this.buffer.length < length) {
      const result = await Promise.race([
        this.reader.read(),
        delay(timeoutMs).then(() => { throw new Error('Timed out waiting for STM32 ROM bootloader.'); }),
      ]);
      if (result.done) throw new Error('Serial port closed while waiting for bootloader response.');
      this.buffer.push(...result.value);
    }
    return Uint8Array.from(this.buffer.splice(0, length));
  }

  async expectAck(context, timeoutMs = 3000) {
    const [value] = await this.readBytes(1, timeoutMs);
    if (value === 0x1f) throw new Error(`STM32 ROM bootloader returned NACK during ${context}.`);
    if (value !== 0x79) throw new Error(`Unexpected bootloader response 0x${value.toString(16).padStart(2, '0')} during ${context}.`);
  }

  async command(command, complement, context) {
    await this.write([command, complement]);
    await this.expectAck(context);
  }

  async getCommands() {
    await this.command(0x00, 0xff, 'Get');
    const length = (await this.readBytes(1))[0] + 1;
    const response = await this.readBytes(length);
    await this.expectAck('Get completion');
    return { version: response[0], commands: response.slice(1) };
  }

  async getDeviceId() {
    await this.command(0x02, 0xfd, 'Get ID');
    const length = (await this.readBytes(1))[0] + 1;
    const response = await this.readBytes(length);
    await this.expectAck('Get ID completion');
    if (response.length < 2) throw new Error('Bootloader returned a malformed device ID.');
    return (response[0] << 8) | response[1];
  }

  async erasePages(pageNumbers, commands) {
    if (commands.includes(0x44)) {
      // Chunk page lists to keep each serial request comfortably bounded.
      for (let offset = 0; offset < pageNumbers.length; offset += 255) {
        checkFirmwareStopRequested();
        const batch = pageNumbers.slice(offset, offset + 255);
        await this.command(0x44, 0xbb, 'extended erase');
        const count = batch.length - 1;
        const body = [(count >> 8) & 0xff, count & 0xff];
        for (const page of batch) body.push((page >> 8) & 0xff, page & 0xff);
        body.push(body.reduce((checksum, byte) => checksum ^ byte, 0));
        await this.write(body);
        await this.expectAck('extended erase', 60000);
      }
      return;
    }
    if (!commands.includes(0x43)) {
      throw new Error('The bootloader does not support erasing this firmware image safely.');
    }
    if (pageNumbers.some((page) => page > 255)) {
      throw new Error('The bootloader only supports 8-bit page numbers; this image needs extended erase for pages above 255. No pages were erased.');
    }
    // Standard erase can list at most 255 pages per request (0xFF is reserved).
    for (let offset = 0; offset < pageNumbers.length; offset += 255) {
      checkFirmwareStopRequested();
      const batch = pageNumbers.slice(offset, offset + 255);
      await this.command(0x43, 0xbc, 'erase');
      const body = [batch.length - 1, ...batch];
      body.push(body.reduce((checksum, byte) => checksum ^ byte, 0));
      await this.write(body);
      await this.expectAck('erase', 60000);
    }
  }

  async writePage(address, data) {
    await this.command(0x31, 0xce, 'Write Memory');
    const addr = [
      (address >>> 24) & 0xff,
      (address >>> 16) & 0xff,
      (address >>> 8) & 0xff,
      address & 0xff,
    ];
    addr.push(addr.reduce((checksum, byte) => checksum ^ byte, 0));
    await this.write(addr);
    await this.expectAck('Write Memory address');
    const body = [data.length - 1, ...data];
    body.push(body.reduce((checksum, byte) => checksum ^ byte, 0));
    await this.write(body);
    await this.expectAck('Write Memory data');
  }

  async readMemory(address, length) {
    await this.command(0x11, 0xee, 'Read Memory');
    const addr = [
      (address >>> 24) & 0xff,
      (address >>> 16) & 0xff,
      (address >>> 8) & 0xff,
      address & 0xff,
    ];
    addr.push(addr.reduce((checksum, byte) => checksum ^ byte, 0));
    await this.write(addr);
    await this.expectAck('Read Memory address');
    const count = length - 1;
    await this.write([count, count ^ 0xff]);
    await this.expectAck('Read Memory length');
    return this.readBytes(length, 10000);
  }

  async close(closePort = true) {
    try { await this.reader.cancel(); } catch { /* stream may already be closed */ }
    try { this.reader.releaseLock(); } catch { /* already released */ }
    try { this.writer.releaseLock(); } catch { /* already released */ }
    if (closePort) {
      try { await this.port.close(); } catch { /* already closed */ }
    }
  }
}

const elements = {
  connect: document.querySelector('#connect-button'),
  connection: document.querySelector('#connection'),
  connectionLabel: document.querySelector('#connection-label'),
  notice: document.querySelector('#notice'),
  rateChart: document.querySelector('#rate-chart'),
  rateTooltip: document.querySelector('#rate-chart-tooltip'),
  deviceDate: document.querySelector('#device-date'),
  deviceTime: document.querySelector('#device-time'),
  settingsDeviceDate: document.querySelector('#settings-device-date'),
  settingsDeviceTime: document.querySelector('#settings-device-time'),
  settingsSystemDate: document.querySelector('#settings-system-date'),
  settingsSystemTime: document.querySelector('#settings-system-time'),
  settingsClockDifference: document.querySelector('#settings-clock-difference'),
  settingsClockStatus: document.querySelector('#settings-clock-status'),
  syncClock: document.querySelector('#sync-clock'),
  overviewUptime: document.querySelector('#overview-uptime'),
  solarPercent: document.querySelector('#solar-percent'),
  batteryPercent: document.querySelector('#battery-percent'),
  overviewTab: document.querySelector('#overview-tab'),
  debugTab: document.querySelector('#debug-tab'),
  settingsTab: document.querySelector('#settings-tab'),
  logsTab: document.querySelector('#logs-tab'),
  firmwareTab: document.querySelector('#firmware-tab'),
  manualTab: document.querySelector('#manual-tab'),
  aboutTab: document.querySelector('#about-tab'),
  overviewPage: document.querySelector('#overview-page'),
  debugPage: document.querySelector('#debug-page'),
  settingsPage: document.querySelector('#settings-page'),
  logsPage: document.querySelector('#logs-page'),
  logTabs: document.querySelector('#log-tabs'),
  logContent: document.querySelector('#log-content'),
  logDate: document.querySelector('#log-date'),
  logsDeviceTime: document.querySelector('#logs-device-time'),
  loadLogDate: document.querySelector('#load-log-date'),
  logsStatus: document.querySelector('#logs-status'),
  loadMoreLogs: document.querySelector('#load-more-logs'),
  firmwarePage: document.querySelector('#firmware-page'),
  localBuildFlash: document.querySelector('#local-build-flash'),
  flashLocalBuild: document.querySelector('#flash-local-build'),
  rebootFirmware: document.querySelector('#reboot-firmware'),
  rebootBootloader: document.querySelector('#reboot-bootloader'),
  rebootStatus: document.querySelector('#reboot-status'),
  firmwareFile: document.querySelector('#firmware-file'),
  firmwareImageStatus: document.querySelector('#firmware-image-status'),
  clearFirmware: document.querySelector('#clear-firmware'),
  clearFirmwareLog: document.querySelector('#clear-firmware-log'),
  firmwareVerify: document.querySelector('#firmware-verify'),
  flashFirmware: document.querySelector('#flash-firmware'),
  firmwareLog: document.querySelector('#firmware-log'),
  dumpFlash: document.querySelector('#dump-flash'),
  dumpEeprom: document.querySelector('#dump-eeprom'),
  dumpSpi: document.querySelector('#dump-spi'),
  dumpStatus: document.querySelector('#dump-status'),
  manualPage: document.querySelector('#manual-page'),
  aboutPage: document.querySelector('#about-page'),
  settingsStatus: document.querySelector('#settings-status'),
  settingsList: document.querySelector('#settings-list'),
  rateFoot: document.querySelector('#rate-foot'),
  rateLabel: document.querySelector('#rate-label'),
  rateDose: document.querySelector('#rate-dose'),
  rateWarmup: document.querySelector('#rate-warmup'),
  rateWarmupFill: document.querySelector('#rate-warmup-fill'),
  rateUnit: document.querySelector('#rate-unit'),
  doseLabel: document.querySelector('#dose-label'),
  dose: document.querySelector('#dose'),
  doseUnit: document.querySelector('#dose-unit'),
  doseFoot: document.querySelector('#dose-foot'),
  uptime: document.querySelector('#uptime'),
  debugBatteryPercent: document.querySelector('#debug-battery-percent'),
  debugBatteryRaw: document.querySelector('#debug-battery-raw'),
  debugBatteryStartPercent: document.querySelector('#debug-battery-start-percent'),
  debugBatteryStartRaw: document.querySelector('#debug-battery-start-raw'),
  debugSolarPercent: document.querySelector('#debug-solar-percent'),
  debugSolarRaw: document.querySelector('#debug-solar-raw'),
  debugChargePercent: document.querySelector('#debug-charge-percent'),
  debugChargeTime: document.querySelector('#debug-charge-time'),
  debugScreenPercent: document.querySelector('#debug-screen-percent'),
  debugScreenTime: document.querySelector('#debug-screen-time'),
  debugSleepPercent: document.querySelector('#debug-sleep-percent'),
  debugSleepTime: document.querySelector('#debug-sleep-time'),
  debugCount: document.querySelector('#debug-count'),
  debugRtc: document.querySelector('#debug-rtc'),
  debugClock: document.querySelector('#debug-clock'),
  debugSeconds: document.querySelector('#debug-seconds'),
  debugDate: document.querySelector('#debug-date'),
  debugTime: document.querySelector('#debug-time'),
  debugModeBadge: document.querySelector('#debug-mode-badge'),
  memorySpi: document.querySelector('#memory-spi'),
  memoryJedec: document.querySelector('#memory-jedec'),
};

class ProtocolError extends Error {}

function crc16CcittFalse(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
      crc &= 0xffff;
    }
  }
  return crc;
}

function cobsEncode(bytes) {
  const output = new Uint8Array(bytes.length + Math.ceil(bytes.length / 254) + 1);
  let read = 0;
  let write = 1;
  let codeIndex = 0;
  let code = 1;
  while (read < bytes.length) {
    if (bytes[read] === 0) {
      output[codeIndex] = code;
      codeIndex = write++;
      code = 1;
      read += 1;
    } else {
      output[write++] = bytes[read++];
      code += 1;
      if (code === 0xff) {
        output[codeIndex] = code;
        codeIndex = write++;
        code = 1;
      }
    }
  }
  output[codeIndex] = code;
  output[write++] = 0;
  return output.slice(0, write);
}

function cobsDecode(bytes) {
  const output = new Uint8Array(bytes.length);
  let read = 0;
  let write = 0;
  while (read < bytes.length) {
    const code = bytes[read++];
    if (code === 0 || read + code - 1 > bytes.length) {
      throw new ProtocolError('Invalid encoded frame.');
    }
    for (let i = 1; i < code; i += 1) output[write++] = bytes[read++];
    if (code !== 0xff && read < bytes.length) output[write++] = 0;
  }
  return output.slice(0, write);
}

function encodeFrame(kind, requestId, opcode, payload = new Uint8Array()) {
  if (payload.length > MAX_PAYLOAD) throw new ProtocolError('Payload is too large.');
  const raw = new Uint8Array(10 + payload.length);
  const view = new DataView(raw.buffer);
  raw[0] = VERSION;
  raw[1] = kind;
  view.setUint16(2, requestId, true);
  view.setUint16(4, opcode, true);
  view.setUint16(6, payload.length, true);
  raw.set(payload, 8);
  view.setUint16(8 + payload.length, crc16CcittFalse(raw.subarray(0, 8 + payload.length)), true);
  return cobsEncode(raw);
}

function decodeFrame(wire) {
  if (wire.length < 2 || wire[wire.length - 1] !== 0) {
    throw new ProtocolError('Frame delimiter is missing.');
  }
  const raw = cobsDecode(wire.subarray(0, wire.length - 1));
  if (raw.length < 10) throw new ProtocolError('Frame is too short.');
  const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
  const payloadLength = view.getUint16(6, true);
  if (raw[0] !== VERSION || raw[1] !== KIND_RESPONSE || payloadLength > MAX_PAYLOAD || raw.length !== 10 + payloadLength) {
    throw new ProtocolError('Unsupported frame version or length.');
  }
  const expectedCrc = view.getUint16(8 + payloadLength, true);
  if (expectedCrc !== crc16CcittFalse(raw.subarray(0, 8 + payloadLength))) {
    throw new ProtocolError('Frame checksum failed.');
  }
  return {
    requestId: view.getUint16(2, true),
    opcode: view.getUint16(4, true),
    payload: raw.slice(8, 8 + payloadLength),
  };
}

class SerialProtocol {
  constructor(onFrame, onClose) {
    this.onFrame = onFrame;
    this.onClose = onClose;
    this.port = null;
    this.portInfo = null;
    this.reader = null;
    this.writer = null;
    this.readTask = null;
    this.closing = false;
    this.requestId = 0;
    this.pending = new Map();
    this.frameBytes = [];
    this.discardFrame = false;
  }

  async connect(portOverride = null) {
    if (!('serial' in navigator)) {
      throw new Error('Web Serial is not available in this browser. Try a supported desktop browser.');
    }
    this.port = portOverride || await navigator.serial.requestPort();
    selectedSerialPort = this.port;
    this.portInfo = this.port.getInfo?.() || {};
    await this.port.open({ baudRate: 115200, dataBits: 8, parity: 'none', stopBits: 1, flowControl: 'none', bufferSize: 256 });
    portMayBeOpen = true;
    // Restore the previously working behavior: release DTR/RTS after open.
    // This can reboot the device; HELLO is retried after its startup delay.
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: false });
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.closing = false;
    this.readTask = this.readLoop();
  }

  async readLoop() {
    let failure = null;
    try {
      while (!this.closing) {
        const { value, done } = await this.reader.read();
        if (done) break;
        for (const byte of value) this.consumeByte(byte);
      }
    } catch (error) {
      failure = error;
    } finally {
      if (this.reader) {
        try { this.reader.releaseLock(); } catch { /* already released */ }
        this.reader = null;
      }
      if (!this.closing) this.onClose(failure || new Error('Serial port closed.'));
    }
  }

  consumeByte(byte) {
    if (byte === 0) {
      if (!this.discardFrame && this.frameBytes.length) {
        try { this.onFrame(decodeFrame(Uint8Array.from([...this.frameBytes, 0]))); }
        catch (error) { showNotice(error.message, 'error'); }
      }
      this.frameBytes = [];
      this.discardFrame = false;
      return;
    }
    if (this.discardFrame) return;
    if (this.frameBytes.length >= 80) {
      this.frameBytes = [];
      this.discardFrame = true;
      return;
    }
    this.frameBytes.push(byte);
  }

  async request(opcode, payload = new Uint8Array(), timeoutMs = 1800) {
    if (!this.writer) throw new Error('Serial port is not open.');
    this.requestId = (this.requestId + 1) & 0xffff;
    if (this.requestId === 0) this.requestId = 1;
    const id = this.requestId;
    const promise = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error('Device did not respond. Check the USB connection and try again.'));
      }, timeoutMs);
      this.pending.set(id, { opcode, resolve, reject, timeout });
    });
    try {
      await this.writer.write(encodeFrame(KIND_REQUEST, id, opcode, payload));
    } catch (error) {
      const pending = this.pending.get(id);
      if (pending) {
        clearTimeout(pending.timeout);
        this.pending.delete(id);
      }
      throw error;
    }
    return promise;
  }

  dispatch(frame) {
    const pending = this.pending.get(frame.requestId);
    if (!pending || pending.opcode !== frame.opcode) return;
    clearTimeout(pending.timeout);
    this.pending.delete(frame.requestId);
    pending.resolve(frame.payload);
  }

  async disconnect() {
    if (this.closing) return;
    this.closing = true;
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timeout);
      pending.reject(new Error('Connection closed.'));
    }
    this.pending.clear();
    if (this.reader) {
      try { await this.reader.cancel(); } catch { /* port may already be gone */ }
    }
    if (this.readTask) await this.readTask.catch(() => {});
    if (this.writer) {
      try { this.writer.releaseLock(); } catch { /* already released */ }
      this.writer = null;
    }
    if (this.port) {
      try {
        await this.port.close();
        portMayBeOpen = false;
      } catch { portMayBeOpen = true; /* expose a retry through the Disconnect button */ }
      this.port = null;
    }
    this.readTask = null;
  }
}

let serial = null;
let offlinePreview = true;
let rateHoverIndex = null;
let selectedSerialPort = null;
let bootloaderPortOpen = false;
let portMayBeOpen = false;
let selectedFirmwareImage = null;
let firmwareFlashBusy = false;
let firmwareStopRequested = false;
let dumpBusy = false;
let pollTimer = 0;
let polling = false;
let protocolCapabilities = 0;
let deviceSettings = null;
let deviceHiddenSettings = null;
let selectedResetAction = 0;
let logInfo = null;
let selectedLogType = 'minutes';
let logRecords = [];
let logStartOffset = 0;
let rateHistoryUnit = 'CPM';
let rateLevel = 'blue';
let logPageTotal = 0;
let logPageLoading = false;
let logPageReloadPending = false;
let logPageGeneration = 0;
let logInfoLoading = false;
let logRefreshBusy = false;
let logCountsRefreshBusy = false;
const logViewCache = new Map();
let logCursor = { time: 0xffffffff, epoch: 0xffff, skip: 0 };
let logHasMore = true;
let logHistorical = false;
let logSelectedDate = "";
let clockSyncBusy = false;
const rateHistory = [];
const demoSettings = Array.from({ length: 29 }, () => 0);
const demoHiddenSettings = new Set([13, 15, 16, 21, 27]);
Object.assign(demoSettings, {
  0: 2, 5: 0, 6: 0, 7: 2, 9: 0, 10: 1, 11: 1, 12: 0, 22: 1,
  15: 0, 16: 4, 21: 3, 23: 1,
});

const remoteSettings = [
  { id: 0, label: 'LED', options: ['OFF', 'ON', 'ALWAYS', 'YELLOW', 'RED', 'CUSTOM'] },
  { id: 1, label: 'CLICKS', options: ['OFF', 'ON', 'ALWAYS', 'YELLOW', 'RED', 'CUSTOM'] },
  { id: 2, label: 'RATE SCREEN', options: ['OFF', 'YELLOW', 'RED', 'CUSTOM'] },
  { id: 3, label: 'RATE LIGHT', options: ['OFF', 'YELLOW', 'RED', 'CUSTOM'] },
  { id: 4, label: 'RATE SOUND', options: ['OFF', 'YELLOW', 'RED', 'CUSTOM'] },
  { id: 16, label: 'RATE CUSTOM', options: ['0.3 µSv/h', '0.4 µSv/h', '0.5 µSv/h', '0.7 µSv/h', '0.9 µSv/h', '1 µSv/h', '2 µSv/h', '5 µSv/h', '10 µSv/h'] },
  { id: 24, label: 'DOSE SCREEN', options: ['OFF', 'YELLOW', 'RED', 'CUSTOM'] },
  { id: 25, label: 'DOSE LIGHT', options: ['OFF', 'YELLOW', 'RED', 'CUSTOM'] },
  { id: 26, label: 'DOSE SOUND', options: ['OFF', 'YELLOW', 'RED', 'CUSTOM'] },
  { id: 27, label: 'DOSE CUSTOM', options: ['5 µSv', '7 µSv', '10 µSv', '30 µSv', '50 µSv', '100 µSv', '500 µSv', '1000 µSv'] },
  { id: 17, label: 'ALARM STOP', options: ['CLEAR', 'BUTTON'] },
  { id: 5, label: 'LED BRIGHT', options: ['AUTO', '1%', '3%', '5%', '10%', '25%', '50%', '80%', '100%'] },
  { id: 6, label: 'SCREEN BRIGHT', options: ['AUTO', '1%', '3%', '5%', '10%', '25%', '50%', '80%', '100%'] },
  { id: 7, label: 'SCREEN TIMER', options: ['1 s', '5 s', '10 s', '30 s', '60 s', '90 s'] },
  { id: 20, label: 'TONE', options: ['STOCK', 'CLASSIC', 'MODERN'] },
  { id: 8, label: 'LEAD ZERO', options: ['SHOW', 'HIDE'] },
  { id: 9, label: 'UNIT', options: ['Sv', 'CNT', 'R'] },
  { id: 10, label: 'RATE PREFIX', options: ['m', 'µ', 'n', 'AUTO'] },
  { id: 11, label: 'DOSE PREFIX', options: ['m', 'µ', 'n', 'AUTO'] },
  { id: 12, label: 'RATE ROW 2', options: [] },
  { id: 22, label: 'DOSE ROW 2', options: [] },
  { id: 23, label: 'DOSE PERIOD', options: ['UPTIME', 'DAY', 'TOTAL'] },
  { id: 21, label: 'LONG PRESS', options: ['300 ms', '500 ms', '700 ms', '900 ms', '1100 ms', '1300 ms'] },
  { id: 15, label: 'HV', options: ['ON', 'OFF'] },
  { id: 13, label: 'LOG MEMORY', options: ['EEPROM', 'SPI', 'OFF'] },
  { id: 14, label: 'RESET', options: ['SETTINGS', 'MIN-LOG', 'HOUR-LOG', 'DAY-LOG', 'ERROR-LOG', 'LOGS', 'EEPROM-LOGS', 'EEPROM', 'SPI', 'ALL'] },
  { id: 18, label: 'POWER OFF', options: [] },
  { id: 19, label: 'REBOOT', options: [] },
];

function formatGroupedNumber(value) {
  return Number(value).toLocaleString('en-US').replace(/,/g, ' ');
}

function percentFromAdc(raw, empty, full) {
  if (raw <= empty) return 0;
  if (raw >= full) return 100;
  return 100 - Math.floor(((full - raw) * 100) / (full - empty));
}

function formatRadiation(valueUsv, prefixSetting, symbol, perHour, digitLimit = null) {
  const prefixes = [
    { name: 'm', factor: 0.001 },
    { name: 'µ', factor: 1 },
    { name: 'n', factor: 1000 },
    { name: 'p', factor: 1000000 },
  ];
  let prefix = prefixSetting;
  if (prefix === 3) {
    prefix = prefixes.findIndex(({ factor }) => {
      const scaled = valueUsv * factor;
      return scaled >= 10 && scaled < 1000;
    });
    if (prefix < 0) prefix = prefixes.findIndex(({ factor }) => valueUsv * factor >= 1);
    if (prefix < 0) prefix = 3;
  }
  const selected = prefixes[Math.min(prefix, prefixes.length - 1)];
  const value = valueUsv * selected.factor;
  const integerDigits = Math.max(0, Math.floor(Math.log10(Math.abs(value))) + 1);
  const maxDecimals = perHour ? 3 : 5;
  const decimalsAvailable = digitLimit === null ? 5 - integerDigits :
    digitLimit - Math.max(1, integerDigits);
  const digits = value === 0 ? 0 : Math.max(0,
    Math.min(maxDecimals, decimalsAvailable));
  const text = value.toFixed(digits).replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '');
  return { value: text, unit: `${selected.name}${symbol}${perHour ? '/h' : ''}` };
}

function drawRateChart() {
  const canvas = elements.rateChart;
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;
  const scale = window.devicePixelRatio || 1;
  const width = Math.round(rect.width * scale);
  const height = Math.round(rect.height * scale);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const context = canvas.getContext('2d');
  context.setTransform(scale, 0, 0, scale, 0, 0);
  const w = rect.width;
  const h = rect.height;
  context.clearRect(0, 0, w, h);
  context.fillStyle = '#819096';
  context.font = '10px "DM Mono", Consolas, monospace';
  context.fillText(rateHistoryUnit, 4, 12);
  const values = offlinePreview ? Array.from({ length: 24 }, (_, index) => index) : rateHistory;
  if (offlinePreview) {
    context.textAlign = 'right';
    context.fillStyle = '#9b83da';
    context.fillText('DEMO · ILLUSTRATIVE', w - 4, 12);
    context.textAlign = 'left';
  }
  if (values.length < 2) return;

  const levelColors = {
    blue: '#287bff', green: '#00e58b', yellow: '#f0d35e', red: '#ff6666',
  };
  const levelColor = levelColors[rateLevel];

  const peak = offlinePreview ? values.length - 1 : Math.max(...values, 0.01) * 1.12;
  const points = values.map((value, index) => ({
    x: (index / (values.length - 1)) * w,
    y: offlinePreview ? h - (index / (values.length - 1)) * h : h - (value / peak) * h,
  }));
  context.strokeStyle = 'rgba(160,186,194,.10)';
  context.lineWidth = 1;
  for (const fraction of [0.25, 0.5, 0.75]) {
    context.beginPath();
    context.moveTo(0, h * fraction);
    context.lineTo(w, h * fraction);
    context.stroke();
  }
  context.beginPath();
  points.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y));
  context.lineTo(w, h);
  context.lineTo(0, h);
  context.closePath();
  context.fillStyle = `${levelColor}18`;
  context.fill();
  context.beginPath();
  points.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y));
  context.strokeStyle = levelColor;
  context.lineWidth = 1.8;
  context.lineJoin = 'round';
  context.lineCap = 'round';
  context.stroke();
  if (!offlinePreview && rateHoverIndex !== null && points[rateHoverIndex]) {
    const point = points[rateHoverIndex];
    context.beginPath();
    context.arc(point.x, point.y, 4, 0, Math.PI * 2);
    context.fillStyle = levelColor;
    context.fill();
    context.lineWidth = 2;
    context.strokeStyle = '#e6edf0';
    context.stroke();
  }
}

function hideRateChartTooltip() {
  rateHoverIndex = null;
  elements.rateTooltip.hidden = true;
}

function showRateChartTooltip(event) {
  if (offlinePreview || !serial || rateHistory.length === 0) {
    hideRateChartTooltip();
    return;
  }
  const canvasRect = elements.rateChart.getBoundingClientRect();
  const panel = elements.rateChart.parentElement;
  const panelRect = panel.getBoundingClientRect();
  let pointerRatio = Math.max(0, Math.min(1,
    (event.clientX - canvasRect.left) / canvasRect.width));
  rateHoverIndex = Math.round(pointerRatio * (rateHistory.length - 1));
  pointerRatio = rateHistory.length > 1 ?
    rateHoverIndex / (rateHistory.length - 1) : 0;
  const value = rateHistory[rateHoverIndex];
  const unit = deviceSettings?.[9] ?? 0;
  const display = unit === 1 ?
    { value: formatGroupedNumber(Math.round(value)), unit: 'CPM' } :
    formatRadiation(value, deviceSettings?.[10] ?? 1,
      unit === 2 ? 'R' : 'Sv', true);
  elements.rateTooltip.textContent = `${display.value} ${display.unit}`;
  elements.rateTooltip.hidden = false;
  drawRateChart();

  const pointX = pointerRatio * canvasRect.width;
  const peak = Math.max(...rateHistory, 0.01) * 1.12;
  const pointY = canvasRect.height - (value / peak) * canvasRect.height;
  const left = canvasRect.left - panelRect.left + pointX + 10;
  const top = canvasRect.top - panelRect.top + pointY -
    elements.rateTooltip.offsetHeight - 8;
  elements.rateTooltip.style.left = `${Math.max(8, Math.min(
    panel.clientWidth - elements.rateTooltip.offsetWidth - 8, left))}px`;
  elements.rateTooltip.style.top = `${Math.max(8, Math.min(
    panel.clientHeight - elements.rateTooltip.offsetHeight - 8, top))}px`;
}

function showNotice(message, kind = '') {
  elements.notice.dataset.kind = kind;
  elements.notice.querySelector('p').textContent = message;
}

function selectPage(page, updateUrl = true) {
  if (page === 'instructions') page = 'manual';
  const pages = ['overview', 'debug', 'settings', 'logs', 'firmware', 'manual', 'about'];
  if (!pages.includes(page)) return;
  for (const name of pages) {
    const active = name === page;
    elements[`${name}Page`].hidden = !active;
    elements[`${name}Tab`].classList.toggle('active', active);
    elements[`${name}Tab`].setAttribute('aria-selected', String(active));
  }
  if (updateUrl && window.location.hash !== `#${page}`) {
    window.history.replaceState(null, '', `${window.location.pathname}${window.location.search}#${page}`);
  }
  if (page === 'overview') drawRateChart();
  if (page === 'settings' && serial && deviceSettings === null) loadSettings();
  if (page === 'settings' && serial && protocolCapabilities && logInfo === null) loadLogInfo();
  if (page === 'logs' && serial && protocolCapabilities && logInfo === null) loadLogInfo();
  else if (page === 'logs' && serial && logInfo) {
    if (logRecords.length === 0) loadLogPage(true);
    else refreshLatestLogRecord();
  }
}

function renderDebugLogInfo() {
  if (!logInfo) return;
  elements.debugRtc.textContent = logInfo.rtcStatus ||
    (logInfo.calendarValid ? 'SET' : 'NOT SET');
  elements.debugClock.textContent = logInfo.clockSource || '—';
  elements.debugSeconds.textContent = formatGroupedNumber(logInfo.seconds);
  elements.memorySpi.textContent = logInfo.spiSizeMb === null ? '—' :
    `${logInfo.spiSizeMb}MB`;
  elements.memorySpi.classList.toggle('state-error', logInfo.spi && !logInfo.spiReady);
  elements.memoryJedec.textContent = logInfo.jedec ? logInfo.jedec : '—';
}

function applyLogInfo(payload) {
  if (payload.length !== 30 || payload[0] !== 0 || payload[26] !== 2) {
    throw new ProtocolError('Update the device firmware to use the new logs.');
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  logInfo = {
    memory: payload[1], source: payload[27], spi: payload[27] === 1,
    minutes: view.getUint16(2, true), hours: view.getUint16(4, true),
    days: view.getUint16(6, true), alerts: view.getUint16(8, true), seconds: payload[29],
    calendarValid: !!payload[10], calendarSeconds: view.getUint32(11, true),
    calendarAnchorSeconds: view.getUint32(11, true), calendarAnchorUptime: view.getUint32(22, true),
    spiSizeMb: payload[15], spiReady: !!payload[16],
    jedec: Array.from(payload.subarray(17, 20), (byte) => byte.toString(16).padStart(2, '0')).join(' ').toUpperCase(),
    rtcStatus: ({1: 'OSC ERR', 2: 'INIT ERR', 3: 'SYNC ERR', 4: 'DBP ERR', 5: 'SET', 6: 'NOT SET', 7: 'NOT READY'}[payload[20]] || 'UNKNOWN'),
    clockSource: payload[21] === 1 ? 'LSE' : 'LSI', error: !!payload[28],
  };
  if (!logHistorical && logInfo.calendarValid) elements.logDate.value = deviceLogDate();
  renderDebugLogInfo();
  for (const type of logTypes) {
    const cell = document.querySelector(`#debug-${type.id}`);
    if (cell) cell.textContent = formatGroupedNumber(logInfo[type.countKey]);
  }
  const store = document.querySelector('#debug-log-store');
  if (store) store.textContent = logInfo.error ? 'ERR' : ['EEPROM', 'SPI', 'OFF'][logInfo.memory];
  elements.logTabs.querySelectorAll('[data-log-type]').forEach((button) => {
    const type = logTypes.find((item) => item.id === button.dataset.logType);
    if (type) button.textContent = `${type.label} (${formatGroupedNumber(logInfo[type.countKey])})`;
  });
  updateDeviceDateTime();
}

async function refreshLogCounts() {
  if (!serial || !logInfo || logCountsRefreshBusy) return;
  logCountsRefreshBusy = true;
  const activeSerial = serial;
  try {
    const payload = await activeSerial.request(OP_GET_LOG_INFO, undefined, 10000);
    if (serial === activeSerial) applyLogInfo(payload);
  } catch { /* The next poll retries a transient transport error. */ }
  finally { logCountsRefreshBusy = false; }
}

const logTypes = [
  { id: 'minutes', protocolId: 0, label: 'MINUTES', countKey: 'minutes', period: 60 },
  { id: 'hours', protocolId: 1, label: 'HOURS', countKey: 'hours', period: 3600 },
  { id: 'days', protocolId: 2, label: 'DAYS', countKey: 'days', period: 86400 },
  { id: 'alerts', protocolId: 3, label: 'ALERTS', countKey: 'alerts', period: 60 },
];

function deviceLogDate() {
  return new Date(Date.UTC(2000, 0, 1) + logInfo.calendarSeconds * 1000).toISOString().slice(0, 10);
}
function logViewKey() { return `${selectedLogType}:${logHistorical ? logSelectedDate : 'latest'}`; }
function saveLogView() {
  logViewCache.set(logViewKey(), {records: logRecords.slice(), cursor: {...logCursor}, more: logHasMore});
}
function restoreLogView() {
  const saved = logViewCache.get(logViewKey());
  if (!saved) return false;
  logRecords = saved.records.slice(); logCursor = {...saved.cursor}; logHasMore = saved.more;
  return true;
}
function clearLogView() {
  logRecords = []; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0}; logHasMore = true;
  logStartOffset = 0; logPageTotal = 0;
}
function hasCurrentLogDate() { return !logHistorical; }
function logDateLimit() {
  if (!logHistorical || !logSelectedDate) return 0;
  const end = Date.parse(`${logSelectedDate}T00:00:00Z`) + 86400000 - 1000;
  return Math.max(0, Math.floor((end - Date.UTC(2000, 0, 1)) / 1000));
}
function logRecordKey(record) {
  return `${record.epoch}:${record.timestamp}:${selectedLogType === 'alerts' ? record.id : 0}`;
}

function renderLogTabs() {
  elements.logTabs.replaceChildren();
  if (!logInfo) return;
  for (const type of logTypes) {
    const button = document.createElement('button'); button.type = 'button';
    button.className = `log-tab${type.id === selectedLogType ? ' active' : ''}`;
    button.textContent = `${type.label} (${formatGroupedNumber(logInfo[type.countKey])})`;
    button.dataset.logType = type.id;
    button.setAttribute('aria-selected', String(type.id === selectedLogType));
    button.addEventListener('click', () => {
      if (selectedLogType === type.id) return;
      saveLogView(); ++logPageGeneration;
      selectedLogType = type.id;
      if (!restoreLogView()) clearLogView();
      renderLogTabs();
      if (logPageLoading) { logPageReloadPending = true; return; }
      if (!logRecords.length) loadLogPage(false);
      else if (hasCurrentLogDate()) loadNewerLogRecords();
    });
    elements.logTabs.append(button);
  }
  renderLogSummary();
}

function formatLogDate(seconds, type, calendar = true) {
  if (!calendar) return `UP ${Math.floor(seconds / 3600)}:${String(Math.floor(seconds / 60) % 60).padStart(2, '0')}`;
  const date = new Date(Date.UTC(2000, 0, 1) + seconds * 1000);
  const day = `${String(date.getUTCFullYear()).slice(-2)}-${String(date.getUTCMonth() + 1).padStart(2, '0')}-${String(date.getUTCDate()).padStart(2, '0')}`;
  if (type === 'days') return day;
  return `${day} ${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}`;
}
function formatLogRadiation(valueUsv, isRate = false) {
  const prefix = deviceSettings?.[isRate ? 10 : 11] ?? 1;
  const symbol = (deviceSettings?.[9] ?? 0) === 2 ? 'R' : 'Sv';
  return formatRadiation(valueUsv * (symbol === 'R' ? 100 : 1), prefix, symbol, isRate);
}
function logValueCells(record) {
  const cnt = (deviceSettings?.[9] ?? 0) === 1;
  const dose = formatLogRadiation(record.dose * 0.000111);
  const average = formatLogRadiation(record.average * 0.00666, true);
  const maximum = formatLogRadiation(record.maximum * 0.00666, true);
  const values = [cnt ? `${formatGroupedNumber(record.dose)} CNT` : `${dose.value} ${dose.unit}`,
    cnt ? `${record.average} CPM` : `${average.value} ${average.unit}`,
    cnt ? `${record.maximum} CPM` : `${maximum.value} ${maximum.unit}`];
  const duration = `${Math.floor(record.covered / 60)}:${String(record.covered % 60).padStart(2, '0')}`;
  return selectedLogType === 'alerts' ? [duration, ...values] :
    [...values, formatLogCoverage(record)];
}
function formatLogCoverage(record) {
  const type = logTypes.find((item) => item.id === selectedLogType);
  const percent = logCoveragePercent(record, type);
  const state = record.live ? 'live' : (record.partial ? 'partial' : 'full');
  return `${percent}% · ${record.covered}s · ${state}`;
}
function logCoveragePercent(record, type = logTypes.find((item) => item.id === selectedLogType)) {
  return Math.round(Math.min(record.covered, type.period) * 100 / type.period);
}
function logCoverageClass(record, type = logTypes.find((item) => item.id === selectedLogType)) {
  const percent = logCoveragePercent(record, type);
  return percent < 30 ? 'coverage-red' :
    (percent < 50 ? 'coverage-yellow' : (percent > 90 ? 'coverage-green' : ''));
}
function logRateClass(cpm) {
  return cpm * 666 > 90000 ? 'level-red' :
    (cpm * 666 >= 40000 ? 'level-yellow' : '');
}
function logDoseClass(record) {
  const averageCpm = record.covered ? record.dose * 60 / record.covered :
    (record.dose ? Infinity : 0);
  return logRateClass(averageCpm);
}
function renderLogSummary() {
  elements.logContent.replaceChildren();
  elements.logDate.disabled = !logInfo?.calendarValid || selectedLogType === 'alerts';
  elements.loadLogDate.disabled = elements.logDate.disabled;
  elements.loadMoreLogs.hidden = !logInfo || !logHasMore;
  elements.loadMoreLogs.disabled = logPageLoading;
  if (!logInfo) return;
  const type = logTypes.find((item) => item.id === selectedLogType);
  elements.logsStatus.textContent = `${formatGroupedNumber(logRecords.length)} records loaded · ${formatGroupedNumber(logInfo[type.countKey])} stored entries${logInfo.memory === 2 ? ' · recording off' : ''}${logInfo.error ? ' · storage error' : ''}`;
  if (!logRecords.length) { elements.logContent.textContent = logPageLoading ? 'Loading records…' : 'No records available.'; return; }
  const header = document.createElement('div'); header.className = 'log-grid log-grid-header';
  header.innerHTML = selectedLogType === 'alerts' ?
    '<span>Date</span><span>Duration</span><span>Dose</span><span>Average</span><span>Maximum</span>' :
    '<span>Date</span><span>Dose</span><span>Average</span><span>Maximum</span><span>Coverage</span>';
  elements.logContent.append(header);
  const maximum = logRecords.reduce((maximum, record) => Math.max(maximum, record.dose), 1);
  for (const record of logRecords) {
    const row = document.createElement('div');
    const isAlert = selectedLogType === 'alerts';
    row.className = 'log-grid log-record';
    row.style.setProperty('--bar-width', `${Math.min(100, record.dose * 100 / maximum)}%`);
    row.title = `Clock period ${record.epoch}${record.live ? ' · live' : ''}${record.partial ? ' · incomplete coverage' : ''}`;
    const values = [formatLogDate(record.timestamp, selectedLogType, record.calendar), ...logValueCells(record)];
    const metricClasses = isAlert ? [null, null, logDoseClass(record),
      logRateClass(record.average), logRateClass(record.maximum)] :
      [null, logDoseClass(record), logRateClass(record.average),
        logRateClass(record.maximum), logCoverageClass(record)];
    for (let index = 0; index < values.length; ++index) {
      const cell = document.createElement('span');
      const isMetric = isAlert ? index >= 2 && index <= 4 : index >= 1 && index <= 3;
      if (isMetric) cell.className = `log-metric${metricClasses[index] ? ` ${metricClasses[index]}` : ''}`;
      else if (metricClasses[index]) cell.className = metricClasses[index];
      cell.textContent = values[index]; row.append(cell);
    }
    elements.logContent.append(row);
  }
}

async function queryLogPage(type, cursor, limit, count = 2) {
  const request = new Uint8Array(18); const view = new DataView(request.buffer);
  request[0] = type.protocolId; request[1] = count;
  view.setUint32(2, type.period, true); view.setUint32(6, cursor.time, true);
  view.setUint16(10, cursor.epoch, true); view.setUint16(12, cursor.skip, true); view.setUint32(14, limit, true);
  const payload = await serial.request(OP_GET_LOG_PAGE, request, 15000);
  if (payload.length < 12 || payload[0] !== 0 || payload[1] !== type.protocolId ||
      payload[2] > count || payload.length !== 12 + payload[2] * 25) throw new ProtocolError('Invalid canonical log response.');
  const data = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const records = [];
  for (let i = 0; i < payload[2]; ++i) {
    const at = 12 + i * 25, flags = payload[at + 22];
    records.push({timestamp: data.getUint32(at, true), dose: data.getUint32(at + 4, true),
      average: data.getUint32(at + 8, true), maximum: data.getUint32(at + 12, true),
      covered: data.getUint32(at + 16, true), epoch: data.getUint16(at + 20, true),
      id: data.getUint16(at + 23, true), available: !!(flags & 1), partial: !!(flags & 2),
      live: !!(flags & 4), calendar: !!(flags & 8)});
  }
  return {records, more: !!payload[3], cursor: {time: data.getUint32(4, true), epoch: data.getUint16(8, true), skip: data.getUint16(10, true)}};
}

async function loadLogNearDate() {
  if (!serial || !logInfo?.calendarValid || !elements.logDate.value) return;
  saveLogView(); ++logPageGeneration;
  logHistorical = elements.logDate.value !== deviceLogDate();
  logSelectedDate = logHistorical ? elements.logDate.value : "";
  if (!restoreLogView()) clearLogView();
  renderLogSummary();
  if (!logRecords.length) await loadLogPage(false);
  else if (hasCurrentLogDate()) await loadNewerLogRecords();
}

async function loadLogPage(reset = false) {
  if (!serial || !logInfo) return;
  if (logPageLoading) { if (reset) ++logPageGeneration; logPageReloadPending = true; return; }
  if (reset) { ++logPageGeneration; clearLogView(); }
  const generation = logPageGeneration, type = logTypes.find((item) => item.id === selectedLogType);
  const target = logRecords.length + LOG_INITIAL_TARGETS[type.id];
  const limit = logDateLimit();
  logPageLoading = true; renderLogSummary();
  try {
    while (generation === logPageGeneration && logHasMore && logRecords.length < target) {
      const batchEnd = Math.min(target, logRecords.length + LOG_BATCH_SIZE);
      while (generation === logPageGeneration && logHasMore && logRecords.length < batchEnd) {
        const page = await queryLogPage(type, logCursor, limit, Math.min(2, batchEnd - logRecords.length));
        if (generation !== logPageGeneration) break;
        const keys = new Set(logRecords.map(logRecordKey));
        for (const record of page.records) if (!keys.has(logRecordKey(record))) { logRecords.push(record); keys.add(logRecordKey(record)); }
        logCursor = page.cursor; logHasMore = page.more && page.records.length > 0;
      }
      if (generation === logPageGeneration) { renderLogSummary(); saveLogView(); }
    }
  } catch (error) {
    if (generation === logPageGeneration) elements.logsStatus.textContent = error.message;
  } finally {
    logPageLoading = false;
    if (generation === logPageGeneration) { logPageTotal = logRecords.length; saveLogView(); elements.loadMoreLogs.disabled = false; }
    if (logPageReloadPending && serial && logInfo) { logPageReloadPending = false; if (!logRecords.length) loadLogPage(false); else if (hasCurrentLogDate()) loadNewerLogRecords(); }
  }
}

async function refreshLatestLogRecord() { return loadNewerLogRecords(false); }
async function loadNewerLogRecords(catchUp = true) {
  if (!serial || !logInfo || logPageLoading || logRefreshBusy || elements.logsPage.hidden || !hasCurrentLogDate()) return;
  if (!logRecords.length) return loadLogPage(false);
  logRefreshBusy = true;
  const generation = logPageGeneration, type = logTypes.find((item) => item.id === selectedLogType);
  const known = new Set(logRecords.map(logRecordKey));
  const fresh = []; let cursor = {time: 0xffffffff, epoch: 0xffff, skip: 0}, boundary = false, more = true;
  try {
    while (!boundary && more && generation === logPageGeneration) {
      let batch = 0;
      while (batch < LOG_BATCH_SIZE && !boundary && more) {
        const page = await queryLogPage(type, cursor, 0, Math.min(2, LOG_BATCH_SIZE - batch));
        if (generation !== logPageGeneration || elements.logsPage.hidden) return;
        for (const record of page.records) { fresh.push(record); if (known.has(logRecordKey(record))) boundary = true; }
        batch += page.records.length; cursor = page.cursor; more = page.more && page.records.length > 0;
        if (!catchUp) boundary = true;
      }
      const replacements = new Map(fresh.map((record) => [logRecordKey(record), record]));
      const older = logRecords.filter((record) => !(type.id === 'alerts' && record.live) && !replacements.has(logRecordKey(record)))
        .map((record) => ({...record, live: false}));
      logRecords = [...replacements.values(), ...older];
      renderLogSummary(); saveLogView();
    }
  } catch (error) {
    if (generation === logPageGeneration) elements.logsStatus.textContent = error.message;
  } finally { logRefreshBusy = false; }
}

async function loadLogInfo() {
  if (!serial || logInfoLoading) return;
  logInfoLoading = true;
  try {
    applyLogInfo(await serial.request(OP_GET_LOG_INFO, undefined, 15000));
    if (!logTypes.some((type) => type.id === selectedLogType) || (!logRecords.length && !logViewCache.size))
      selectedLogType = logTypes.find((type) => logInfo[type.countKey] > 0)?.id || 'minutes';
    renderLogTabs(); updateSpiDumpAvailability();
    if (!elements.logsPage.hidden && !logRecords.length) loadLogPage(false);
  } catch (error) { elements.logsStatus.textContent = error.message; }
  finally { logInfoLoading = false; }
}

async function synchronizeDeviceClock() {
  if (!serial || (protocolCapabilities & CAP_SET_RTC) === 0 || clockSyncBusy) return;
  const systemNow = new Date();
  if (systemNow.getFullYear() < 2000 || systemNow.getFullYear() > 2099) {
    elements.settingsClockStatus.dataset.level = 'error';
    elements.settingsClockStatus.textContent = 'System date must be between 2000 and 2099.';
    return;
  }

  clockSyncBusy = true;
  elements.syncClock.textContent = 'Synchronizing…';
  elements.settingsClockStatus.dataset.level = '';
  elements.settingsClockStatus.textContent = 'Writing system time to the device…';
  updateRtcControls();
  try {
    const secondsSince2000 = localCalendarSeconds(systemNow);
    const request = new Uint8Array(4);
    new DataView(request.buffer).setUint32(0, secondsSince2000, true);
    const syncStartedAt = Date.now();
    const response = await serial.request(OP_SET_RTC, request, 5000);
    if (response.length !== 1 || response[0] !== 0) {
      throw new ProtocolError('The device rejected the clock update.');
    }
    while (logInfoLoading) await delay(20);
    await loadLogInfo();
    if (logInfo) {
      logInfo.calendarValid = true;
      logInfo.calendarSeconds = secondsSince2000 +
        Math.floor((Date.now() - syncStartedAt) / 1000);
      updateDeviceDateTime();
    } else {
      renderSettingsClock();
    }
    elements.settingsClockStatus.dataset.level = 'success';
    elements.settingsClockStatus.textContent = 'Device clock synchronized to system time.';
  } catch (error) {
    elements.settingsClockStatus.dataset.level = 'error';
    elements.settingsClockStatus.textContent = error.message;
  } finally {
    clockSyncBusy = false;
    elements.syncClock.textContent = 'Synchronize';
    updateRtcControls();
  }
}

function setConnection(state, label) {
  const canDisconnect = state === 'connected' || state === 'disconnect-pending';
  elements.connection.dataset.state = state;
  elements.connectionLabel.textContent = label;
  elements.connect.disabled = state === 'connecting';
  elements.connect.textContent = canDisconnect ? 'Disconnect' :
    (state === 'connecting' ? 'Connecting…' : 'Connect');
  elements.connect.classList.toggle('button-primary', !canDisconnect);
  elements.connect.classList.toggle('button-quiet', canDisconnect);
  const liveData = state === 'connected' && !offlinePreview;
  elements.debugModeBadge.textContent = liveData ? 'DEVICE DATA' :
    (state === 'disconnected' || state === 'disconnect-pending' ?
      'DEMO DATA' : 'CONNECTING');
  elements.debugModeBadge.dataset.mode = liveData ? 'device' : 'demo';
  if (state !== 'connected') hideRateChartTooltip();
  updateRtcControls();
}

function formatUptime(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = seconds % 60;
  return {
    clock: [hours, minutes, secs].map((n) => String(n).padStart(2, '0')).join(':'),
    days,
  };
}

function formatDebugUsage(seconds, uptimeSeconds) {
  const percent = uptimeSeconds ? Math.min(100,
    Math.floor((seconds * 100 + uptimeSeconds / 2) / uptimeSeconds)) : 0;
  const hours = Math.floor(seconds / 3600);
  const hoursText = String(hours % 10000).padStart(4, '0');
  const minutes = String(Math.floor((seconds % 3600) / 60)).padStart(2, '0');
  return {
    percent: String(percent).padStart(3, '0'),
    time: `${hoursText}:${minutes}`,
  };
}

function localCalendarSeconds(date) {
  return Math.floor((date.getTime() - date.getTimezoneOffset() * 60000) / 1000) -
    946684800;
}

function formatLocalDate(date) {
  return [date.getFullYear(), String(date.getMonth() + 1).padStart(2, '0'),
    String(date.getDate()).padStart(2, '0')].join('-');
}

function formatLocalTime(date) {
  return [date.getHours(), date.getMinutes(), date.getSeconds()]
    .map((part) => String(part).padStart(2, '0')).join(':');
}

function formatDeviceDate(date) {
  return [date.getUTCFullYear(), String(date.getUTCMonth() + 1).padStart(2, '0'),
    String(date.getUTCDate()).padStart(2, '0')].join('-');
}

function formatDeviceTime(date) {
  return [date.getUTCHours(), date.getUTCMinutes(), date.getUTCSeconds()]
    .map((part) => String(part).padStart(2, '0')).join(':');
}

function formatClockDifference(milliseconds) {
  let remaining = Math.abs(milliseconds);
  const units = [
    [86400000, 'd'],
    [3600000, 'h'],
    [60000, 'm'],
    [1000, 's'],
    [1, 'ms'],
  ];
  const parts = [];
  for (const [unitMs, label] of units) {
    const value = Math.floor(remaining / unitMs);
    remaining %= unitMs;
    if (value > 0) parts.push(`${value}${label}`);
  }
  return parts.join(' ') || '0ms';
}

function renderSettingsClock() {
  const systemNow = new Date();
  elements.settingsSystemDate.textContent = formatLocalDate(systemNow);
  elements.settingsSystemTime.textContent = formatLocalTime(systemNow);
  if (!logInfo?.calendarValid) {
    elements.settingsDeviceDate.textContent = 'NOT SET';
    elements.settingsDeviceTime.textContent = '—';
    elements.settingsClockDifference.textContent = '—';
    elements.settingsClockDifference.dataset.state = 'neutral';
    return;
  }

  const deviceDate = new Date(Date.UTC(2000, 0, 1) + logInfo.calendarSeconds * 1000);
  elements.settingsDeviceDate.textContent = formatDeviceDate(deviceDate);
  elements.settingsDeviceTime.textContent = formatDeviceTime(deviceDate);
  const differenceMs = logInfo.calendarSeconds * 1000 -
    localCalendarSeconds(systemNow) * 1000 - systemNow.getMilliseconds();
  const sign = differenceMs > 0 ? '+' : (differenceMs < 0 ? '−' : '');
  elements.settingsClockDifference.textContent =
    `${sign}${formatClockDifference(differenceMs)}`;
  elements.settingsClockDifference.dataset.state = differenceMs > 0 ? 'positive' :
    (differenceMs < 0 ? 'negative' : 'neutral');
}

function updateRtcControls() {
  const supported = !!serial &&
    elements.connection.dataset.state === 'connected' &&
    (protocolCapabilities & CAP_SET_RTC) !== 0;
  elements.syncClock.disabled = !supported || clockSyncBusy;
  if (!supported && !clockSyncBusy && (
    elements.settingsClockStatus.textContent.startsWith('This firmware') ||
    elements.settingsClockStatus.textContent.startsWith('Clock synchronization is available.') ||
    elements.settingsClockStatus.textContent.startsWith('Connect to compare or synchronize')
  )) {
    elements.settingsClockStatus.dataset.level = '';
    elements.settingsClockStatus.textContent = '';
  }
}

function updateDeviceDateTime(uptimeText = null) {
  if (!logInfo?.calendarValid) {
    elements.deviceDate.textContent = '—';
    elements.deviceTime.textContent = '—';
    elements.debugDate.textContent = '---- -- --';
    elements.debugTime.textContent = '--:--:--';
    elements.logsDeviceTime.textContent = '—';
  } else {
    const date = new Date(Date.UTC(2000, 0, 1) + logInfo.calendarSeconds * 1000);
    elements.deviceDate.textContent = [date.getUTCFullYear(),
      String(date.getUTCMonth() + 1).padStart(2, '0'),
      String(date.getUTCDate()).padStart(2, '0')].join('-');
    elements.deviceTime.textContent = `${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}`;
    elements.debugDate.textContent = elements.deviceDate.textContent;
    elements.debugTime.textContent = `${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}:${String(date.getUTCSeconds()).padStart(2, '0')}`;
    elements.logsDeviceTime.textContent = `${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}:${String(date.getUTCSeconds()).padStart(2, '0')}`;
  }
  renderSettingsClock();
  if (uptimeText !== null) elements.overviewUptime.textContent = uptimeText;
}

function handleHello(payload) {
  if (payload.length !== 10 || payload[0] !== 0) throw new ProtocolError('Device rejected HELLO or returned an unknown format.');
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const protocolVersion = payload[3];
  const maxPayload = view.getUint16(4, true);
  const capabilities = view.getUint32(6, true);
  if (protocolVersion !== VERSION || maxPayload < 25 || (capabilities & 1) === 0) {
    throw new ProtocolError(`Unsupported protocol or capabilities (version ${protocolVersion}, payload ${maxPayload}).`);
  }
  protocolCapabilities = capabilities;
  updateRtcControls();
  updateSpiDumpAvailability();
}

function updateSpiDumpAvailability() {
  if (!elements.dumpSpi) return;
  const supported = !!serial && (protocolCapabilities & CAP_GET_SPI_DUMP) !== 0 &&
    !!logInfo?.spi && !!logInfo?.spiReady && (logInfo.spiSizeMb || 0) > 0;
  elements.dumpSpi.disabled = dumpBusy || !supported;
}

function settingsOptions(setting, currentSettings = deviceSettings) {
  if ((setting.id === 10 || setting.id === 11) && currentSettings?.[9] === 1) {
    return ['CPM', 'CPS'];
  }
  if (setting.id === 12) {
    const unit = currentSettings?.[9] ?? 0;
    return [
      {label: 'CPM', value: unit === 1 ? 'duplicate-cpm' : 0, disabled: unit === 1},
      {label: 'CPS', value: unit === 1 ? 3 : 1, disabled: false},
      {label: 'OFF', value: 2, disabled: false},
      {label: 'SV', value: unit === 1 ? 0 : (unit === 0 ? 'duplicate-sv' : 3), disabled: unit === 0},
      {label: 'R', value: unit === 1 ? 1 : (unit === 2 ? 'duplicate-r' : 3), disabled: unit === 2},
    ];
  }
  if (setting.id === 22) {
    const unit = currentSettings?.[9] ?? 0;
    const period = currentSettings?.[23] ?? 1;
    let mode = 1;
    const units = ['SV', 'CNT', 'R'].map((label, value) =>
      value === unit ? {label, value: `duplicate-unit-${value}`, disabled: true} :
        {label, value: mode++, disabled: false});
    let time, uptime, day, total;
    if (period === 0) {
      time = mode++;
      uptime = 'duplicate-period-0';
      day = mode++;
      total = mode++;
    } else {
      uptime = mode++;
      day = period === 1 ? 'duplicate-period-1' : mode++;
      total = period === 2 ? 'duplicate-period-2' : mode++;
      time = mode++;
    }
    return [
      {label: 'OFF', value: 0, disabled: false}, ...units,
      {label: 'CPM', value: 6, disabled: false},
      {label: 'TIME', value: time, disabled: false},
      {label: 'UPTIME', value: uptime, disabled: period === 0},
      {label: 'DAY', value: day, disabled: period === 1},
      {label: 'TOTAL', value: total, disabled: period === 2},
    ];
  }
  return setting.options;
}

function settingLabel(setting) {
  return setting.label;
}

function renderSettings() {
  elements.settingsList.replaceChildren();
  const preview = deviceSettings === null;
  const currentSettings = preview ? demoSettings : deviceSettings;
  const rows = [];
  for (const setting of remoteSettings) {
    const supportSettings = deviceSettings ?? demoSettings;
    const hasSetting = supportSettings !== null &&
      setting.id < supportSettings.length;
    const actionSupported = !serial ||
      ![14, 18, 19].includes(setting.id) ||
      (protocolCapabilities & CAP_DEVICE_ACTION) !== 0;
    const supported = hasSetting && actionSupported;
    const optionSettings = preview ? demoSettings :
      (supported ? currentSettings : demoSettings);
    const options = settingsOptions(setting, optionSettings).map((option, index) =>
      typeof option === 'string' ?
        { label: option, value: index, disabled: false } : option);
    const row = document.createElement('div');
    row.className = 'setting-row';
    row.dataset.supported = String(supported);
    const label = document.createElement('label');
    label.htmlFor = `setting-${setting.id}`;
    label.textContent = settingLabel(setting);
    const controls = document.createElement('div');
    controls.className = 'setting-controls';
    if (!supported && !preview) {
      const supportNote = document.createElement('span');
      supportNote.className = 'setting-support-note';
      supportNote.textContent = 'Unsupported';
      controls.append(supportNote);
    }
    const isReset = setting.id === 14;
    const isPowerOff = setting.id === 18;
    const isReboot = setting.id === 19;
    const standaloneActionButton = isPowerOff || isReboot ?
      makeDeviceActionButton(isPowerOff ? 'POWER OFF' : 'REBOOT',
        isPowerOff ? 1 : 2) : null;
    if (standaloneActionButton && (!supported || preview)) {
      standaloneActionButton.disabled = true;
      standaloneActionButton.title = preview ? 'Connect a device to enable this action.' :
        'This device firmware does not support this action.';
    }
    if (setting.id === 13 || isReset) row.classList.add('setting-danger');
    if (isPowerOff || isReboot) row.classList.add('setting-action-warning');

    if (isReset) {
      const select = document.createElement('select');
      select.id = `setting-${setting.id}`;
      options.forEach((item) => {
        const option = document.createElement('option');
        option.value = String(item.value);
        option.textContent = item.label;
        select.append(option);
      });
      select.value = String(selectedResetAction);
      select.disabled = !preview && (!supported ||
        (protocolCapabilities & CAP_DEVICE_ACTION) === 0);
      select.addEventListener('change', () => {
        selectedResetAction = Number(select.value);
      });
      controls.append(select);
      const resetButton = makeDeviceActionButton('Reset', 0, () => selectedResetAction);
      if (preview || !supported) resetButton.disabled = true;
      controls.append(resetButton);
    } else if (standaloneActionButton) {
      // Destructive device actions replace the text heading in this row.
    } else {
      const select = document.createElement('select');
      select.id = `setting-${setting.id}`;
      select.dataset.settingId = String(setting.id);
      options.forEach((item) => {
        const option = document.createElement('option');
        option.value = String(item.value);
        option.textContent = item.label;
        option.disabled = item.disabled;
        select.append(option);
      });
      const selectedValue = optionSettings[setting.id] ?? 0;
      select.value = String(selectedValue);
      const inactiveSetting = setting.id === 11 && optionSettings[9] === 1;
      select.disabled = (!preview && !supported) || inactiveSetting;
      select.addEventListener('change', () => {
        const nextValue = Number(select.value);
        if (preview) {
          demoSettings[setting.id] = nextValue;
          if (setting.id === 9) {
            const prefixDefault = nextValue === 1 ? 0 : 1;
            demoSettings[10] = prefixDefault;
            demoSettings[11] = prefixDefault;
          }
          if (setting.id === 13) {
            document.querySelector('#debug-log-store').textContent =
              ['EEPROM', 'SPI', 'OFF'][nextValue] || 'EEPROM';
          }
          if (setting.id === 23) demoSettings[22] = nextValue === 0 ? 3 : 1;
          if (setting.id === 9 || setting.id === 23) {
            renderSettings();
            document.querySelector(`#setting-${setting.id}`)?.focus();
          }
          return;
        }
        if (supported && !inactiveSetting) {
          if (setting.id === 13 && nextValue === 1 &&
              deviceSettings[13] !== 1 && !window.confirm(
                'Enabling SPI will erase all assets stored on the SPI chip. After that, returning to stock firmware will not be possible. Continue?')) {
            select.value = String(deviceSettings[13]);
            return;
          }
          writeSetting(setting.id, nextValue, select);
        }
      });
      controls.append(select);
    }

    if (setting.id !== 18) {
      const visibilityLabel = document.createElement('label');
      visibilityLabel.className = 'setting-visible';
      const visibility = document.createElement('input');
      visibility.type = 'checkbox';
      visibility.checked = preview ? !demoHiddenSettings.has(setting.id) :
        deviceHiddenSettings === null || (deviceHiddenSettings[setting.id] ?? false) === false;
      visibility.disabled = !preview && (!supported || deviceHiddenSettings === null);
      visibility.title = 'Show this setting on the device';
      visibility.addEventListener('change', () => {
        if (preview) {
          if (visibility.checked) demoHiddenSettings.delete(setting.id);
          else demoHiddenSettings.add(setting.id);
          return;
        }
        writeSetting(0x80 + setting.id, visibility.checked ? 0 : 1, visibility);
      });
      const visibilityText = document.createElement('span');
      visibilityText.textContent = 'Show';
      visibilityLabel.append(visibility, visibilityText);
      controls.append(visibilityLabel);
    }
    if (standaloneActionButton) row.append(standaloneActionButton);
    else row.append(label);
    row.append(controls);
    rows.push(row);
  }
  const pair = document.createElement('div');
  pair.className = 'settings-pair';
  pair.style.setProperty('--settings-row-count', Math.ceil(rows.length / 2));
  pair.append(...rows);
  elements.settingsList.append(pair);
  if (preview) {
    elements.settingsStatus.textContent = serial ?
      (protocolCapabilities ?
        'Settings unavailable in this firmware; showing factory defaults.' :
        'Waiting for device settings; showing factory defaults.') :
      'Offline preview · temporary changes; reload to restore factory defaults.';
  } else if (deviceSettings) {
    const unsupportedCount = remoteSettings.filter(({ id }) =>
      id >= deviceSettings.length || ([14, 18, 19].includes(id) &&
        (protocolCapabilities & CAP_DEVICE_ACTION) === 0)).length;
    elements.settingsStatus.textContent = unsupportedCount ?
      `Loaded from device · ${unsupportedCount} unsupported items disabled` : 'Loaded from device';
  }
  document.querySelector('.settings-note').textContent = preview ?
    'Demo controls are temporary. Reload the page to restore factory defaults; device actions remain disabled.' :
    'Use Show to control which settings appear in the device menu. Visibility requires firmware protocol support.';
}

function renderOfflinePreview() {
  elements.solarPercent.textContent = '0%';
  elements.solarPercent.classList.remove('solar-warn', 'solar-good');
  elements.batteryPercent.textContent = '0%';
  elements.deviceTime.textContent = '--:--:--';
  elements.deviceDate.textContent = '---- -- --';
  elements.overviewUptime.textContent = '00:00:00';
  elements.rateLabel.textContent = 'RATE';
  elements.rateDose.textContent = '0';
  elements.rateDose.classList.remove('level-blue', 'level-yellow', 'level-red');
  elements.rateDose.classList.add('level-green');
  rateLevel = 'green';
  elements.rateUnit.textContent = 'µSv/h';
  elements.rateFoot.textContent = '0 CPM';
  elements.rateFoot.hidden = false;
  elements.rateWarmup.hidden = true;
  elements.doseLabel.textContent = 'DOSE TODAY';
  elements.dose.textContent = '0';
  elements.doseUnit.textContent = 'µSv';
  elements.doseFoot.hidden = true;
  elements.uptime.textContent = '0D 00:00:00';
  elements.debugDate.textContent = '---- -- --';
  elements.debugTime.textContent = '--:--:--';
  elements.debugCount.textContent = '0';
  elements.debugRtc.textContent = 'NOT SET';
  elements.debugClock.textContent = 'LSI';
  elements.debugBatteryPercent.textContent = '0%';
  elements.debugBatteryRaw.textContent = '0';
  elements.debugBatteryStartPercent.textContent = '0%';
  elements.debugBatteryStartRaw.textContent = '0';
  elements.debugSolarPercent.textContent = '0%';
  elements.debugSolarRaw.textContent = '0';
  for (const [percentElement, timeElement] of [
    [elements.debugChargePercent, elements.debugChargeTime],
    [elements.debugScreenPercent, elements.debugScreenTime],
    [elements.debugSleepPercent, elements.debugSleepTime],
  ]) {
    const usage = formatDebugUsage(0, 0);
    percentElement.textContent = usage.percent;
    timeElement.textContent = usage.time;
  }
  for (const type of logTypes) {
    const cell = document.querySelector(`#debug-${type.id}`);
    if (cell) cell.textContent = '0';
  }
  document.querySelector('#debug-log-store').textContent = 'EEPROM';
  elements.memorySpi.textContent = '1 MB';
  elements.memoryJedec.textContent = '—';
  elements.debugSeconds.textContent = '0';
  elements.rateChart.setAttribute('aria-label', 'Illustrative demo graph, not device data');
  elements.logsStatus.textContent = 'Demo preview · no device logs loaded';
  const demoLogTypes = logTypes.map(({ id, label }) => [id, label]);
  elements.logTabs.replaceChildren();
  elements.logContent.replaceChildren();
  const renderDemoLog = (type) => {
    elements.logTabs.querySelectorAll('[data-demo-log]').forEach((tab) =>
      tab.classList.toggle('active', tab.dataset.demoLog === type));
    elements.logContent.replaceChildren();
    const header = document.createElement('div');
    header.className = 'log-grid log-grid-header';
    const alert = type === 'alerts';
    const columns = alert ? ['DATE', 'DURATION', 'DOSE', 'AVERAGE', 'MAXIMUM'] :
      ['DATE', 'DOSE', 'AVERAGE', 'MAXIMUM', 'COVERAGE'];
    columns.forEach((text) => {
      const cell = document.createElement('span');
      cell.textContent = text;
      header.append(cell);
    });
    const record = document.createElement('div');
    record.className = 'log-grid log-record';
    const demoType = logTypes.find((item) => item.id === type);
    const values = alert ? ['—', '0:00', '0 µSv', '0 µSv/h', '0 µSv/h'] :
      ['—', '0 µSv', '0 µSv/h', '0 µSv/h', `100% · ${demoType.period}s · full`];
    values.forEach((text, index) => {
      const cell = document.createElement('span');
      if (!alert && index === values.length - 1) cell.className = 'coverage-green';
      else if (alert ? index >= 2 : index >= 1 && index <= 3) cell.className = 'log-metric';
      cell.textContent = text;
      record.append(cell);
    });
    elements.logContent.append(header, record);
    elements.logsStatus.textContent = `DEMO DATA · 1 illustrative ${type} record`;
  };
  demoLogTypes.forEach(([id, label]) => {
    const tab = document.createElement('button');
    tab.type = 'button';
    tab.className = 'log-tab';
    tab.dataset.demoLog = id;
    tab.textContent = `${label} (1)`;
    tab.addEventListener('click', () => renderDemoLog(id));
    elements.logTabs.append(tab);
  });
  renderDemoLog(selectedLogType);
  elements.settingsDeviceTime.textContent = '--:--:--';
  elements.settingsDeviceDate.textContent = '---- -- --';
  elements.settingsClockDifference.textContent = '—';
  elements.settingsSystemTime.textContent = new Date().toLocaleTimeString('en-GB');
  elements.settingsSystemDate.textContent = new Date().toLocaleDateString('en-CA');
  elements.settingsClockStatus.textContent = 'Demo preview · clock controls are unavailable.';
  elements.settingsClockStatus.dataset.level = '';
  elements.syncClock.disabled = true;
  renderSettings();
  drawRateChart();
}

function makeDeviceActionButton(label, action, valueProvider = () => 0) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'button button-quiet device-action-button';
  button.textContent = label;
  button.disabled = !serial || (protocolCapabilities & CAP_DEVICE_ACTION) === 0;
  button.addEventListener('click', () =>
    runDeviceAction(action, valueProvider(), button));
  return button;
}

async function loadSettings() {
  if (!serial) return;
  if ((protocolCapabilities & (CAP_GET_SETTINGS | CAP_SET_SETTING)) !==
      (CAP_GET_SETTINGS | CAP_SET_SETTING)) {
    elements.settingsStatus.textContent = '';
    renderSettings();
    return;
  }
  elements.settingsStatus.textContent = 'Reading…';
  try {
    const payload = await serial.request(OP_GET_SETTINGS, Uint8Array.of(1));
    if (payload.length < 2 || payload[0] !== 0 ||
        ![21, 22, 24, 28, 29].includes(payload[1]) ||
        (payload.length !== payload[1] + 2 &&
         payload.length !== payload[1] + 6)) {
      throw new ProtocolError('Invalid settings response.');
    }
    const count = payload[1];
    deviceSettings = Array.from(payload.subarray(2, 2 + count));
    deviceHiddenSettings = null;
    if (payload.length === count + 6) {
      const offset = 2 + count;
      const mask = (payload[offset] | (payload[offset + 1] << 8) |
        (payload[offset + 2] << 16) | (payload[offset + 3] << 24)) >>> 0;
      deviceHiddenSettings = Array.from({ length: count }, (_, id) =>
        (mask & (1 << id)) !== 0);
      if (deviceHiddenSettings[18] &&
          (protocolCapabilities & CAP_SET_SETTING) !== 0) {
        const result = await serial.request(OP_SET_SETTING,
          Uint8Array.of(0x80 + 18, 0));
        if (result.length === 1 && result[0] === 0) {
          deviceHiddenSettings[18] = false;
        }
      }
    }
    renderSettings();
    if (logInfo && logRecords.length) renderLogSummary();
    elements.settingsStatus.textContent = 'Loaded from device';
  } catch (error) {
    elements.settingsStatus.textContent = error.message;
  }
}

async function writeSetting(settingId, value, control) {
  if (!serial) return;
  control.disabled = true;
  elements.settingsStatus.textContent = 'Saving…';
  try {
    const result = await serial.request(OP_SET_SETTING, Uint8Array.of(settingId, value), settingId === 13 ? 120000 : 5000);
    if (result.length !== 1 || result[0] !== 0) throw new ProtocolError('Device rejected this setting.');
    if (settingId === 9) {
      rateHistory.length = 0;
      drawRateChart();
    }
    deviceSettings = null;
    await loadSettings();
    if (settingId === 13 && deviceSettings) {
      logInfo = null;
      logRecords = [];
      await loadLogInfo();
    }
    if (deviceSettings) elements.settingsStatus.textContent = 'Applied on device';
  } catch (error) {
    const message = error.message;
    await loadSettings();
    elements.settingsStatus.textContent = message;
  } finally {
    control.disabled = false;
  }
}

async function runDeviceAction(action, value, button) {
  if (!serial || (protocolCapabilities & CAP_DEVICE_ACTION) === 0) return;
  const resetWarnings = [
    'Reset all settings to defaults? The device will reboot.',
    'Clear minute logs? This cannot be undone.',
    'Clear hourly logs? This cannot be undone.',
    'Clear daily logs? This cannot be undone.',
    'Clear alert logs? This cannot be undone.',
    'Clear all logs? This cannot be undone.',
    'Clear EEPROM logs? This cannot be undone and may also clear settings stored in the EEPROM log area.',
    'Erase the entire EEPROM, including settings and logs? The device will restart.',
    'Erase SPI data? This may remove assets needed by stock firmware.',
    'Erase all EEPROM and SPI data? This cannot be undone; the device will restart.',
  ];
  if (action === 0 && !window.confirm(resetWarnings[value] ||
      'Perform this reset? This cannot be undone.')) return;

  button.disabled = true;
  elements.settingsStatus.textContent = 'Sending action…';
  try {
    const result = await serial.request(OP_DEVICE_ACTION,
      Uint8Array.of(action, value));
    if (result.length !== 1 || result[0] !== 0) {
      throw new ProtocolError('Device rejected this action.');
    }
    elements.settingsStatus.textContent = action === 0 ?
      `Reset requested: ${remoteSettings.find((item) => item.id === 14).options[value]}` :
      action === 1 ? 'Power off requested' : 'Reboot requested';
    if (action === 0 && ![0, 7, 9].includes(value)) {
      logInfo = null;
      logRecords = [];
      logViewCache.clear(); logHistorical = false; logSelectedDate = ""; logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
      logPageTotal = 0;
      await loadLogInfo();
    }
  } catch (error) {
    elements.settingsStatus.textContent = error.message;
  } finally {
    button.disabled = false;
  }
}

function formatDoseUptime(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = seconds % 60;
  if (days > 1) return `${days}D ${String(hours).padStart(2, '0')}H ${String(minutes).padStart(2, '0')}M`;
  const elapsedHours = Math.floor(seconds / 3600);
  return [elapsedHours, minutes, secs].map((part) => String(part).padStart(2, '0')).join(':');
}

function rateRowText(mode, unit, cpm, rateUsvh, ratePrefix) {
  if (unit === 1) {
    if (mode === 2) return '';
    if (mode === 0 || mode === 1) {
      const symbol = mode === 0 ? 'Sv' : 'R';
      const value = mode === 0 ? rateUsvh : rateUsvh * 100;
      const formatted = formatRadiation(value, 1, symbol, true);
      return `${formatted.value} ${formatted.unit}`;
    }
    if (mode === 3) return `${(cpm / 60).toFixed(2)} CPS`;
  } else {
    if (mode === 2) return '';
    if (mode === 0) return `${formatGroupedNumber(cpm)} CPM`;
    if (mode === 1) return `${(cpm / 60).toFixed(2)} CPS`;
    if (mode === 3) {
      const symbol = unit === 0 ? 'R' : 'Sv';
      const value = unit === 0 ? rateUsvh * 100 : rateUsvh;
      const formatted = formatRadiation(value, ratePrefix, symbol, true);
      return `${formatted.value} ${formatted.unit}`;
    }
  }
  return '';
}

function doseRowChoice(unit, period, mode) {
  const choices = ['OFF'];
  ['Sv', 'CNT', 'R'].forEach((_, value) => {
    if (value !== unit) choices.push({ kind: 'unit', value });
  });
  if (period === 0) {
    choices.push({ kind: 'time' }, { kind: 'period', value: 1 }, { kind: 'period', value: 2 });
  } else {
    [0, 1, 2].forEach((value) => {
      if (value !== period) choices.push({ kind: 'period', value });
    });
    choices.push({ kind: 'time' });
  }
  choices.push('CPM');
  return choices[mode] || 'OFF';
}

function doseValueText(unit, pulses, dosePrefix) {
  if (unit === 1) return `${formatGroupedNumber(pulses)} CNT`;
  const doseUsv = pulses * (400 / 3600) / 1000;
  const symbol = unit === 2 ? 'R' : 'Sv';
  const formatted = formatRadiation(doseUsv * (unit === 2 ? 100 : 1), dosePrefix, symbol, false, 6);
  return `${formatted.value} ${formatted.unit}`;
}

function doseRowText(unit, period, mode, cpm, uptime, uptimePulses, dayPulses, totalPulses, dosePrefix) {
  const choice = doseRowChoice(unit, period, mode);
  if (choice === 'OFF') return '';
  if (choice.kind === 'time') return formatDoseUptime(uptime);
  if (choice === 'CPM') return `${formatGroupedNumber(cpm)} CPM`;
  if (choice.kind === 'unit') {
    const pulses = period === 0 ? uptimePulses : period === 1 ? dayPulses : totalPulses;
    return doseValueText(choice.value, pulses, dosePrefix);
  }
  const pulses = choice.value === 0 ? uptimePulses : choice.value === 1 ? dayPulses : totalPulses;
  return doseValueText(unit, pulses, dosePrefix);
}

function handleStatus(payload) {
  if (![25, 29, 31, 35, 47].includes(payload.length) || payload[0] !== 0) throw new ProtocolError('Device rejected GET_STATUS or returned an unknown format.');
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const uptimeSeconds = view.getUint32(1, true);
  const count = view.getUint32(5, true);
  const cpm = view.getUint32(9, true);
  const doseNsvWhole = view.getUint32(13, true);
  const doseFraction = payload.length >= 31 ? view.getUint16(29, true) : 0;
  const doseNsv = doseNsvWhole + doseFraction / 3600;
  const batteryRaw = view.getUint16(17, true);
  const solarRaw = view.getUint16(19, true);
  const countTotal = payload.length >= 29 ? view.getUint32(25, true) : count;
  const lifetimePulses = payload.length >= 35 ? view.getUint32(31, true) : countTotal;
  const screenSeconds = payload.length >= 47 ? view.getUint32(35, true) : null;
  const sleepSeconds = payload.length >= 47 ? view.getUint32(39, true) : null;
  const usbSeconds = payload.length >= 47 ? view.getUint32(43, true) : null;
  const statusFlags = view.getUint16(23, true);
  const batteryStartRaw = payload.length >= 47 ? (statusFlags >>> 4) & 0x0fff : null;
  const dosePeriod = deviceSettings?.[23] ?? 1;
  const doseRowMode = deviceSettings?.[22] ?? 0;
  const derivedDosePulses = payload.length >= 31 ?
    Math.min(0xffffffff, Math.floor((doseNsvWhole * 3600 + doseFraction + 200) / 400)) :
      (dosePeriod === 0 ? countTotal : count);
  const uptimePulses = countTotal;
  const dayPulses = count;
  const dosePulseCount = payload.length >= 35 ?
    (dosePeriod === 0 ? uptimePulses : dosePeriod === 1 ? dayPulses : lifetimePulses) :
    derivedDosePulses;
  rateLevel = countTotal < 30 ? 'blue' :
    (cpm * 666 > 90000 ? 'red' : (cpm * 666 >= 40000 ? 'yellow' : 'green'));
  const levelClass = `level-${rateLevel}`;
  elements.rateDose.classList.remove('level-blue', 'level-green', 'level-yellow', 'level-red');
  elements.rateDose.classList.add(levelClass);
  const warmupPulses = Math.min(countTotal, 30);
  elements.rateWarmup.hidden = rateLevel !== 'blue';
  elements.rateWarmup.setAttribute('aria-valuenow', String(warmupPulses));
  elements.rateWarmupFill.style.width = `${(warmupPulses / 30) * 100}%`;
  const uptime = formatUptime(uptimeSeconds);
  if (logInfo?.calendarValid) {
    if (logInfo.calendarAnchorUptime === null) {
      logInfo.calendarAnchorUptime = uptimeSeconds;
      logInfo.calendarAnchorSeconds = logInfo.calendarSeconds;
    }
    logInfo.calendarSeconds = logInfo.calendarAnchorSeconds +
      Math.max(0, uptimeSeconds - logInfo.calendarAnchorUptime);
  }
  const unit = deviceSettings?.[9] ?? 0;
  const ratePrefix = deviceSettings?.[10] ?? 1;
  const dosePrefix = deviceSettings?.[11] ?? 1;
  const rateUsvh = cpm * 0.00666 * (unit === 2 ? 100 : 1);
  const doseUsv = doseNsv / 1000 * (unit === 2 ? 100 : 1);
  const dosePeriodLabels = ['DOSE UPTIME', 'DOSE TODAY', 'DOSE TOTAL'];
  elements.doseLabel.textContent = dosePeriodLabels[dosePeriod] || 'DOSE TODAY';

  if (unit === 1) {
    elements.rateLabel.textContent = 'RATE';
    elements.rateDose.textContent = formatGroupedNumber(cpm);
    elements.rateUnit.textContent = 'CPM';
    elements.dose.textContent = formatGroupedNumber(dosePulseCount);
    elements.doseUnit.textContent = 'CNT';
  } else {
    const symbol = unit === 2 ? 'R' : 'Sv';
    const rate = formatRadiation(rateUsvh, ratePrefix, symbol, true);
    const dose = formatRadiation(doseUsv, dosePrefix, symbol, false, 6);
    elements.rateLabel.textContent = 'RATE';
    elements.rateDose.textContent = rate.value;
    elements.rateUnit.textContent = rate.unit;
    elements.dose.textContent = dose.value;
    elements.doseUnit.textContent = dose.unit;
  }
  const rateRowMode = deviceSettings?.[12] ?? 0;
  const rateRow = rateRowText(rateRowMode, unit, cpm, rateUsvh,
    deviceSettings?.[10] ?? 1);
  elements.rateFoot.textContent = rateRow;
  elements.rateFoot.hidden = rateRow.length === 0;
  let doseRow = doseRowText(unit, dosePeriod, doseRowMode, cpm, uptimeSeconds,
    uptimePulses, dayPulses, lifetimePulses, dosePrefix);
  elements.doseFoot.textContent = doseRow;
  elements.doseFoot.hidden = doseRow.length === 0;
  elements.debugCount.textContent = formatGroupedNumber(countTotal);
  elements.uptime.textContent = `${uptime.days}D ${uptime.clock}`;
  updateDeviceDateTime(`${uptime.days}D ${uptime.clock}`);
  rateHistory.push(unit === 1 ? cpm : rateUsvh);
  rateHistoryUnit = unit === 1 ? 'CPM' :
    formatRadiation(rateUsvh, ratePrefix, unit === 2 ? 'R' : 'Sv', true).unit;
  elements.rateChart.setAttribute('aria-label', `Recent rate history (${rateHistoryUnit})`);
  if (rateHistory.length > 180) rateHistory.shift();
  drawRateChart();
  const solarPercent = Math.min(100, Math.floor(solarRaw / 31));
  elements.solarPercent.textContent = `${solarPercent}%`;
  elements.solarPercent.classList.toggle('solar-warn', solarPercent > 49 && solarPercent <= 79);
  elements.solarPercent.classList.toggle('solar-good', solarPercent > 79);
  const batteryPercent = percentFromAdc(batteryRaw, 1300, 2000);
  elements.batteryPercent.textContent = `${batteryPercent}%`;
  elements.debugBatteryPercent.textContent = `${batteryPercent}%`;
  elements.debugBatteryRaw.textContent = formatGroupedNumber(batteryRaw);
  elements.debugBatteryStartPercent.textContent = batteryStartRaw === null ? '—' :
    `${percentFromAdc(batteryStartRaw, 1300, 2000)}%`;
  elements.debugBatteryStartRaw.textContent = batteryStartRaw === null ? '—' :
    formatGroupedNumber(batteryStartRaw);
  elements.debugSolarPercent.textContent = `${solarPercent}%`;
  elements.debugSolarRaw.textContent = formatGroupedNumber(solarRaw);
  for (const [seconds, percentElement, timeElement] of [
    [usbSeconds, elements.debugChargePercent, elements.debugChargeTime],
    [screenSeconds, elements.debugScreenPercent, elements.debugScreenTime],
    [sleepSeconds, elements.debugSleepPercent, elements.debugSleepTime],
  ]) {
    if (seconds === null) {
      percentElement.textContent = '—';
      timeElement.textContent = '—';
    } else {
      const usage = formatDebugUsage(seconds, uptimeSeconds);
      percentElement.textContent = usage.percent;
      timeElement.textContent = usage.time;
    }
  }
}

function setStateValue(element, active, onText, offText) {
  element.textContent = active ? onText : offText;
  element.classList.toggle('state-on', active);
  element.classList.toggle('state-off', !active);
}

async function refreshStatus() {
  if (!serial || polling) return;
  polling = true;
  try {
    const payload = await serial.request(OP_GET_STATUS);
    handleStatus(payload);
    showNotice('Connected. Readings update once per second.', 'success');
  } catch (error) {
    showNotice(error.message, 'error');
  } finally {
    polling = false;
  }
}

async function requestHelloAfterBoot(protocol) {
  // Briefly allow the device to settle after opening the serial port.
  await delay(1000);
  let lastError;
  for (let attempt = 0; attempt < 4; attempt += 1) {
    try {
      return await protocol.request(OP_HELLO, new Uint8Array(), 1800);
    } catch (error) {
      lastError = error;
      if (attempt < 3) {
        showNotice(`Waiting for device startup… (${attempt + 1}/4)`, '');
        await delay(500);
      }
    }
  }
  if (lastError) lastError.code = 'APP_HELLO_NO_RESPONSE';
  throw lastError;
}

elements.overviewTab.addEventListener('click', () => selectPage('overview'));
elements.debugTab.addEventListener('click', () => selectPage('debug'));
elements.settingsTab.addEventListener('click', () => selectPage('settings'));
elements.syncClock.addEventListener('click', synchronizeDeviceClock);
elements.logsTab.addEventListener('click', () => selectPage('logs'));
elements.loadMoreLogs.addEventListener('click', () => loadLogPage(false));
elements.loadLogDate.addEventListener('click', loadLogNearDate);
elements.firmwareTab.addEventListener('click', () => selectPage('firmware'));
elements.rebootFirmware.addEventListener('click', rebootToFirmware);
elements.rebootBootloader.addEventListener('click', rebootToBootloader);
elements.firmwareFile.addEventListener('change', () => loadFirmwareImage());
elements.flashLocalBuild.addEventListener('click', flashLocalBuild);
elements.clearFirmware.addEventListener('click', clearFirmwareSelection);
elements.flashFirmware.addEventListener('click', flashSelectedFirmware);
elements.dumpFlash.addEventListener('click', () => dumpRomMemory(
  'FLASH', STM32_FLASH_BASE, STM32_FLASH_SIZE, 'flash'));
elements.dumpEeprom.addEventListener('click', () => dumpRomMemory(
  'EEPROM', STM32_EEPROM_BASE, STM32_EEPROM_SIZE, 'eeprom'));
elements.dumpSpi.addEventListener('click', dumpSpiMemory);
elements.manualTab.addEventListener('click', () => selectPage('manual'));
elements.aboutTab.addEventListener('click', () => selectPage('about'));
elements.clearFirmwareLog.addEventListener('click', () => { elements.firmwareLog.textContent = ''; });
window.addEventListener('hashchange', () => {
  const page = window.location.hash.slice(1);
  selectPage(page || 'overview');
});
selectPage(window.location.hash.slice(1) || 'overview');
window.addEventListener('resize', drawRateChart);
detectLocalBuild();

function appendFirmwareLog(message, level = 'normal') {
  const timestamp = new Date().toLocaleTimeString();
  const line = document.createElement('span');
  if (level !== 'normal') {
    line.className = `log-${level}`;
    if (level === 'success') line.style.setProperty('color', '#00e58b', 'important');
    if (level === 'partial') line.style.setProperty('color', '#b8d94c', 'important');
    if (level === 'warning') line.style.setProperty('color', '#f2c94c', 'important');
    if (level === 'error') line.style.setProperty('color', '#ff7474', 'important');
  }
  line.textContent = `[${timestamp}] ${message}\n`;
  elements.firmwareLog.append(line);
  elements.firmwareLog.scrollTop = elements.firmwareLog.scrollHeight;
}

function setRebootStatus(message, level = 'normal') {
  elements.rebootStatus.textContent = message;
  elements.rebootStatus.dataset.level = level;
}

function parseIntelHex(text) {
  const image = new Uint8Array(STM32_FLASH_SIZE).fill(0xff);
  const present = new Uint8Array(STM32_FLASH_SIZE);
  let upperAddress = 0;
  let eof = false;
  for (const [lineIndex, rawLine] of text.split(/\r?\n/).entries()) {
    const line = rawLine.trim();
    if (!line) continue;
    if (eof || line[0] !== ':' || (line.length & 1) !== 1) throw new Error(`Invalid Intel HEX record on line ${lineIndex + 1}.`);
    const record = new Uint8Array((line.length - 1) / 2);
    for (let i = 0; i < record.length; i += 1) {
      const pair = line.slice(1 + i * 2, 3 + i * 2);
      if (!/^[0-9a-f]{2}$/i.test(pair)) throw new Error(`Invalid Intel HEX byte on line ${lineIndex + 1}.`);
      record[i] = Number.parseInt(pair, 16);
    }
    const length = record[0];
    if (record.length !== length + 5 || record.reduce((sum, byte) => (sum + byte) & 0xff, 0) !== 0) {
      throw new Error(`Intel HEX length/checksum error on line ${lineIndex + 1}.`);
    }
    const address = (record[1] << 8) | record[2];
    const type = record[3];
    const data = record.subarray(4, 4 + length);
    if (type === 0x00) {
      const absolute = upperAddress + address;
      if (absolute < STM32_FLASH_BASE || absolute + length > STM32_FLASH_BASE + STM32_FLASH_SIZE) {
        throw new Error(`HEX data at 0x${absolute.toString(16)} is outside STM32L051 flash.`);
      }
      const offset = absolute - STM32_FLASH_BASE;
      for (let i = 0; i < length; i += 1) {
        if (present[offset + i]) throw new Error(`Overlapping HEX data at 0x${(absolute + i).toString(16)}.`);
        image[offset + i] = data[i];
        present[offset + i] = 1;
      }
    } else if (type === 0x01) {
      if (length !== 0 || address !== 0) throw new Error(`Malformed HEX EOF record on line ${lineIndex + 1}.`);
      eof = true;
    } else if (type === 0x02) {
      if (length !== 2 || address !== 0) throw new Error(`Malformed HEX segment address on line ${lineIndex + 1}.`);
      upperAddress = (((data[0] << 8) | data[1]) << 4);
    } else if (type === 0x04) {
      if (length !== 2 || address !== 0) throw new Error(`Malformed HEX linear address on line ${lineIndex + 1}.`);
      upperAddress = (((data[0] << 8) | data[1]) << 16);
    } else if (type === 0x03 || type === 0x05) {
      if (length !== 4 || address !== 0) throw new Error(`Malformed HEX start-address record on line ${lineIndex + 1}.`);
    } else {
      throw new Error(`Unsupported Intel HEX record type ${type} on line ${lineIndex + 1}.`);
    }
  }
  if (!eof) throw new Error('Intel HEX EOF record is missing.');
  return { image, present };
}

function validateFirmwareImage(image, present) {
  if (!present.slice(0, 8).every(Boolean)) throw new Error('Firmware image is missing its initial vector table.');
  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  const stack = view.getUint32(0, true);
  const reset = view.getUint32(4, true);
  const resetAddress = reset & ~1;
  if ((stack & 3) !== 0 || stack < 0x20000000 || stack > 0x20002000) {
    throw new Error(`Invalid initial stack pointer 0x${stack.toString(16)} for STM32L051.`);
  }
  if ((reset & 1) === 0 || resetAddress < STM32_FLASH_BASE || resetAddress >= STM32_FLASH_BASE + STM32_FLASH_SIZE || !present[resetAddress - STM32_FLASH_BASE]) {
    throw new Error(`Invalid reset vector 0x${reset.toString(16)} for STM32L051 flash.`);
  }
  const pageNumbers = new Set();
  for (let i = 0; i < present.length; i += 1) if (present[i]) pageNumbers.add(Math.floor(i / STM32_FLASH_PAGE_SIZE));
  if (pageNumbers.size === 0) throw new Error('Firmware image contains no data.');
  return [...pageNumbers].sort((a, b) => a - b).map((number) => ({
    number,
    address: STM32_FLASH_BASE + number * STM32_FLASH_PAGE_SIZE,
    data: image.slice(number * STM32_FLASH_PAGE_SIZE, (number + 1) * STM32_FLASH_PAGE_SIZE),
  }));
}

async function classifyFirmwareImage(image, present) {
  let hasProjectMarker = false;
  for (let offset = 0; offset <= image.length - PROJECT_IMAGE_MARKER.length; offset += 1) {
    let matches = true;
    for (let i = 0; i < PROJECT_IMAGE_MARKER.length; i += 1) {
      if (!present[offset + i] || image[offset + i] !== PROJECT_IMAGE_MARKER[i]) {
        matches = false;
        break;
      }
    }
    if (matches) {
      hasProjectMarker = true;
      break;
    }
  }

  if (!globalThis.crypto?.subtle) return hasProjectMarker ? 'ours-incompatible' : 'unknown';
  const digest = new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', image));
  const hash = [...digest].map((byte) => byte.toString(16).padStart(2, '0')).join('');
  if (COMPATIBLE_PROJECT_IMAGE_SHA256.has(hash)) return 'ours';
  if (PARTIALLY_COMPATIBLE_PROJECT_IMAGE_SHA256.has(hash)) return 'ours-partial';

  const stockSignatureEnd = STOCK_FAMILY_SIGNATURE_OFFSET + STOCK_FAMILY_SIGNATURE_LENGTH;
  const stockSignaturePresent = present.subarray(STOCK_FAMILY_SIGNATURE_OFFSET, stockSignatureEnd);
  if (stockSignaturePresent.length === STOCK_FAMILY_SIGNATURE_LENGTH && stockSignaturePresent.every(Boolean)) {
    const signatureHash = new Uint8Array(await globalThis.crypto.subtle.digest(
      'SHA-256', image.subarray(STOCK_FAMILY_SIGNATURE_OFFSET, stockSignatureEnd),
    ));
    const signatureHex = [...signatureHash].map((byte) => byte.toString(16).padStart(2, '0')).join('');
    if (signatureHex === STOCK_FAMILY_SIGNATURE_SHA256) return 'stock';
  }
  if (hash === KNOWN_STOCK_IMAGE_SHA256) return 'stock';
  if (hasProjectMarker) return 'ours-incompatible';
  return 'unknown';
}

function setFirmwareImageStatus(text, kind = 'unknown') {
  elements.firmwareImageStatus.textContent = text;
  elements.firmwareImageStatus.dataset.kind = kind;
}

async function loadFirmwareImage(fileOverride = undefined) {
  const file = fileOverride === undefined ? elements.firmwareFile.files?.[0] : fileOverride;
  selectedFirmwareImage = null;
  elements.flashFirmware.disabled = true;
  if (!file) {
    setFirmwareImageStatus('No image selected.');
    return;
  }
  setFirmwareImageStatus('Checking image…');
  try {
    const extension = file.name.toLowerCase().split('.').pop();
    if (extension !== 'hex' && extension !== 'bin') throw new Error('Choose a .hex or .bin firmware image.');
    const buffer = await file.arrayBuffer();
    let parsed;
    if (extension === 'hex') {
      parsed = parseIntelHex(new TextDecoder().decode(buffer));
    } else {
      if (buffer.byteLength === 0 || buffer.byteLength > STM32_FLASH_SIZE) throw new Error('BIN must contain between 1 byte and 64 KiB.');
      const image = new Uint8Array(STM32_FLASH_SIZE).fill(0xff);
      const present = new Uint8Array(STM32_FLASH_SIZE);
      image.set(new Uint8Array(buffer));
      present.fill(1, 0, buffer.byteLength);
      parsed = { image, present };
    }
    const pages = validateFirmwareImage(parsed.image, parsed.present);
    const identity = await classifyFirmwareImage(parsed.image, parsed.present);
    const imageBytes = parsed.present.reduce((sum, byte) => sum + byte, 0);
    const identityLabels = {
      ours: 'Our firmware · JS-compatible',
      'ours-partial': 'Our firmware · partially compatible',
      'ours-incompatible': 'Our firmware · compatibility unknown',
      stock: 'Stock firmware',
      unknown: 'Unknown firmware',
    };
    selectedFirmwareImage = { name: file.name, extension, pages, identity };
    setFirmwareImageStatus(`${identityLabels[identity]} · ${imageBytes.toLocaleString()} / ${STM32_FLASH_SIZE.toLocaleString()} flash bytes · ${pages.length}/${STM32_FLASH_PAGE_COUNT} pages`, identity);
    appendFirmwareLog(`Image accepted: ${identityLabels[identity]}; ${imageBytes.toLocaleString()} flash data bytes in ${pages.length} page(s), within the ${STM32_FLASH_SIZE.toLocaleString()}-byte flash.`);
    elements.flashFirmware.disabled = firmwareFlashBusy;
  } catch (error) {
    setFirmwareImageStatus(`Invalid image · ${error.message}`, 'invalid');
    appendFirmwareLog(`Image rejected: ${error.message}`, 'error');
  }
}

function isLocalDevelopmentHost() {
  return ['localhost', '127.0.0.1', '::1', '[::1]'].includes(window.location.hostname);
}

function localBuildUrl() {
  return new URL('../.pio/build/geiger2/firmware.bin', document.baseURI);
}

async function detectLocalBuild() {
  if (!['http:', 'https:'].includes(window.location.protocol) || !isLocalDevelopmentHost()) return;
  try {
    const response = await fetch(localBuildUrl(), { cache: 'no-store' });
    if (!response.ok) return;
    const contentLength = response.headers.get('content-length');
    if (contentLength !== null && Number(contentLength) > STM32_FLASH_SIZE) return;
    const bytes = await response.arrayBuffer();
    if (bytes.byteLength === 0 || bytes.byteLength > STM32_FLASH_SIZE) return;
    elements.localBuildFlash.hidden = false;
  } catch {
    // Static servers that don't expose the project build directory simply hide this shortcut.
  }
}

let localBuildFlashBusy = false;

async function flashLocalBuild() {
  if (firmwareFlashBusy) {
    await flashSelectedFirmware();
    return;
  }
  if (localBuildFlashBusy) return;
  localBuildFlashBusy = true;
  elements.flashLocalBuild.disabled = true;
  try {
    const response = await fetch(localBuildUrl(), { cache: 'no-store' });
    if (!response.ok) throw new Error(`Could not read the local build (HTTP ${response.status}).`);
    const bytes = await response.arrayBuffer();
    const file = new File([bytes], 'firmware.bin', { type: 'application/octet-stream' });
    await loadFirmwareImage(file);
    if (!selectedFirmwareImage) throw new Error('The local build failed firmware image validation.');
    localBuildFlashBusy = false;
    elements.flashLocalBuild.disabled = false;
    elements.flashLocalBuild.textContent = 'Stop';
    await flashSelectedFirmware();
  } catch (error) {
    appendFirmwareLog(`Local build flash failed: ${error.message}`, 'error');
    setFirmwareImageStatus(`Invalid local build · ${error.message}`, 'invalid');
  } finally {
    localBuildFlashBusy = false;
    elements.flashLocalBuild.disabled = firmwareFlashBusy;
    if (!firmwareFlashBusy) elements.flashLocalBuild.textContent = 'Flash current build';
  }
}

function clearFirmwareSelection() {
  elements.firmwareFile.value = '';
  selectedFirmwareImage = null;
  elements.flashFirmware.disabled = true;
  setFirmwareImageStatus('No image selected.');
  appendFirmwareLog('Firmware image selection cleared.');
}

function downloadDump(bytes, filename) {
  const url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function makeDumpFilename(kind) {
  const now = new Date();
  const datePart = [now.getFullYear(), String(now.getMonth() + 1).padStart(2, '0'),
    String(now.getDate()).padStart(2, '0')].join('-');
  const timePart = [now.getHours(), now.getMinutes(), now.getSeconds()]
    .map((part) => String(part).padStart(2, '0')).join('-');
  return `${kind.toLowerCase()}-${datePart}_${timePart}.bin`;
}

function setDumpBusy(busy) {
  dumpBusy = busy;
  elements.dumpFlash.disabled = busy;
  elements.dumpEeprom.disabled = busy;
  updateSpiDumpAvailability();
}

function setDumpStatus(message, level = '') {
  elements.dumpStatus.textContent = message;
  elements.dumpStatus.dataset.level = level;
}

async function dumpRomMemory(label, address, length, filename) {
  if (dumpBusy) return;
  if (!('serial' in navigator)) {
    setDumpStatus('Web Serial is unavailable in this browser.', 'error');
    return;
  }
  let port = selectedSerialPort;
  try {
    if (!port) port = await navigator.serial.requestPort();
  } catch (error) {
    setDumpStatus(`Could not select the serial port: ${error.message}`, 'error');
    return;
  }
  selectedSerialPort = port;
  const wasConnected = !!serial;
  const dumpFilename = makeDumpFilename(filename);
  let session = null;
  let attemptedBootloader = false;
  let bytes = null;
  let failure = null;
  let restartFailure = null;
  setDumpBusy(true);
  setDumpStatus(`Reading ${label} through the STM32 ROM bootloader…`);
  setConnection('connecting', 'Entering bootloader…');
  appendFirmwareLog(`${label} dump requested (${length.toLocaleString()} bytes).`);
  try {
    attemptedBootloader = true;
    session = await openBootloaderForImage('hex');
    const deviceId = await session.client.getDeviceId();
    if (deviceId !== STM32_DEVICE_ID) {
      throw new Error(`Expected STM32L051 device ID 0x${STM32_DEVICE_ID.toString(16)}, received 0x${deviceId.toString(16)}.`);
    }
    if (!session.info.commands.includes(0x11)) {
      throw new Error('The ROM bootloader does not support Read Memory.');
    }
    bytes = new Uint8Array(length);
    const chunkSize = 256;
    let nextProgress = 10;
    for (let offset = 0; offset < length; offset += chunkSize) {
      const count = Math.min(chunkSize, length - offset);
      bytes.set(await session.client.readMemory(address + offset, count), offset);
      const percent = Math.floor(((offset + count) * 100) / length);
      if (percent >= nextProgress) {
        appendFirmwareLog(`${label} read ${percent}%.`);
        nextProgress = Math.floor(percent / 10 + 1) * 10;
      }
    }
    appendFirmwareLog(`${label} dump read complete.`, 'success');
  } catch (error) {
    failure = error;
    appendFirmwareLog(`${label} dump failed: ${error.message}`, 'error');
  } finally {
    if (session) await session.client.close(false);
    bootloaderPortOpen = false;
    if (attemptedBootloader) {
      try {
        await sendFirmwareStartSignal(port);
        appendFirmwareLog('Firmware-start signal sent after dump.');
      } catch (error) {
        const restartError = new Error(`Could not send the firmware-start signal: ${error.message}`);
        restartFailure = restartError;
        appendFirmwareLog(restartError.message, 'error');
      }
    }
    if (wasConnected && !portMayBeOpen) {
      await connectDevice(port);
    } else if (!portMayBeOpen) {
      setConnection('disconnected', 'Disconnected');
    } else {
      setConnection('disconnect-pending', 'Disconnect required');
    }
    setDumpBusy(false);
  }
  if (failure) {
    setDumpStatus(`${label} dump failed: ${failure.message}`, 'error');
    return;
  }
  downloadDump(bytes, dumpFilename);
  setDumpStatus(restartFailure
    ? `${label} dump saved as ${dumpFilename}, but firmware restart failed: ${restartFailure.message}`
    : `${label} dump saved as ${dumpFilename} (${length.toLocaleString()} bytes).`,
  restartFailure ? 'warning' : 'success');
}

async function dumpSpiMemory() {
  if (dumpBusy) return;
  if (!serial || (protocolCapabilities & CAP_GET_SPI_DUMP) === 0 ||
      !logInfo?.spi || !logInfo?.spiReady || !logInfo.spiSizeMb) {
    setDumpStatus('SPI dump requires connected compatible firmware with SPI logging enabled and ready.', 'warning');
    updateSpiDumpAvailability();
    return;
  }
  const length = logInfo.spiSizeMb * 1024 * 1024;
  const filename = makeDumpFilename('spi');
  const bytes = new Uint8Array(length);
  setDumpBusy(true);
  setDumpStatus(`Reading SPI flash (${length.toLocaleString()} bytes)…`);
  appendFirmwareLog(`SPI dump requested (${length.toLocaleString()} bytes).`);
  try {
    let nextProgress = 10;
    for (let offset = 0; offset < length; offset += SPI_DUMP_CHUNK_SIZE) {
      const count = Math.min(SPI_DUMP_CHUNK_SIZE, length - offset);
      const request = new Uint8Array(5);
      const requestView = new DataView(request.buffer);
      requestView.setUint32(0, offset, true);
      request[4] = count;
      const response = await serial.request(OP_GET_SPI_DUMP_PAGE, request, 5000);
      if (response.length !== count + 6 || response[0] !== 0 ||
          new DataView(response.buffer, response.byteOffset, response.byteLength).getUint32(1, true) !== offset ||
          response[5] !== count) {
        throw new ProtocolError(`Invalid SPI dump response at offset 0x${offset.toString(16)}.`);
      }
      bytes.set(response.subarray(6), offset);
      const percent = Math.floor(((offset + count) * 100) / length);
      if (percent >= nextProgress) {
        appendFirmwareLog(`SPI read ${percent}%.`);
        nextProgress = Math.floor(percent / 10 + 1) * 10;
      }
    }
    downloadDump(bytes, filename);
    appendFirmwareLog(`SPI dump complete and saved as ${filename}.`, 'success');
    setDumpStatus(`SPI dump saved as ${filename} (${length.toLocaleString()} bytes).`, 'success');
  } catch (error) {
    appendFirmwareLog(`SPI dump failed: ${error.message}`, 'error');
    setDumpStatus(`SPI dump failed: ${error.message}`, 'error');
  } finally {
    setDumpBusy(false);
  }
}

async function openBootloaderForImage(mode) {
  const port = selectedSerialPort || await navigator.serial.requestPort();
  selectedSerialPort = port;
  clearInterval(pollTimer);
  pollTimer = 0;
  if (serial) {
    await serial.disconnect();
    serial = null;
  }
  // This is the known-working ROM entry state. File-specific levels are tried
  // only after synchronization, so a BIN signal cannot strand the app before ACK.
  const entrySignals = { dataTerminalReady: true, requestToSend: false };
  if (bootloaderPortOpen) {
    const client = new Stm32Bootloader(port);
    try {
      const info = await client.getCommands();
      appendFirmwareLog(`ROM bootloader is already active (version 0x${info.version.toString(16)}).`, 'success');
      return { client, info, port };
    } catch (error) {
      await client.close(false);
      try { await port.close(); portMayBeOpen = false; } catch { portMayBeOpen = true; }
      bootloaderPortOpen = false;
      appendFirmwareLog(`Existing bootloader session did not answer: ${error.message}`, 'error');
    }
  }

  // First check whether ROM mode is already active, without applying a reset pulse.
  try {
    await port.open({ baudRate: 115200, dataBits: 8, parity: 'even', stopBits: 1, flowControl: 'none', bufferSize: 512 });
    portMayBeOpen = true;
    await port.setSignals(entrySignals);
    await delay(250);
    const client = new Stm32Bootloader(port);
    try {
      await client.write([0x7f]);
      await client.expectAck('ROM synchronization', 1200);
      const info = await client.getCommands();
      bootloaderPortOpen = true;
      appendFirmwareLog(`ROM bootloader already active (version 0x${info.version.toString(16)}).`, 'success');
      return { client, info, port };
    } catch (error) {
      await client.close(false);
      appendFirmwareLog(`Bootloader probe: ${error.message}`, 'error');
    }
    await port.close();
    portMayBeOpen = false;
  } catch (error) {
    try { await port.close(); portMayBeOpen = false; } catch { portMayBeOpen = true; }
    appendFirmwareLog(`Bootloader probe could not open the port: ${error.message}`, 'error');
  }

  let lastError = null;
  for (let attempt = 1; attempt <= 3; attempt += 1) {
    checkFirmwareStopRequested();
    let client = null;
    try {
      appendFirmwareLog(`Bootloader entry attempt ${attempt}/3 (RTS low, DTR high).`);
      await port.open({ baudRate: 115200, dataBits: 8, parity: 'even', stopBits: 1, flowControl: 'none', bufferSize: 512 });
      portMayBeOpen = true;
      await port.setSignals({ dataTerminalReady: true, requestToSend: true });
      await delay(1000);
      checkFirmwareStopRequested();
      await port.setSignals(entrySignals);
      await delay(250);
      client = new Stm32Bootloader(port);
      await client.write([0x7f]);
      await client.expectAck('ROM synchronization', 1500);
      const info = await client.getCommands();
      bootloaderPortOpen = true;
      appendFirmwareLog(`ROM bootloader synchronized (version 0x${info.version.toString(16)}).`, 'success');
      return { client, info, port };
    } catch (error) {
      lastError = error;
      if (client) await client.close(false);
      try { await port.close(); portMayBeOpen = false; } catch { portMayBeOpen = true; }
      appendFirmwareLog(`Attempt ${attempt} failed: ${error.message}`, 'error');
    }
  }
  bootloaderPortOpen = false;
  throw new Error(`Could not enter STM32 ROM bootloader after three attempts${lastError ? `: ${lastError.message}` : '.'}`);
}

async function applyBinSignalModeOrFallback(session) {
  const { port } = session;
  const binClient = session.client;
  checkFirmwareStopRequested();
  appendFirmwareLog('Trying BIN signal mode (RTS low, DTR low) after ROM synchronization…');
  await port.setSignals({ dataTerminalReady: false, requestToSend: false });
  await delay(250);

  try {
    checkFirmwareStopRequested();
    const info = await binClient.getCommands();
    session.info = info;
    appendFirmwareLog('BIN signal mode kept the ROM bootloader link active.', 'success');
    return false;
  } catch (error) {
    await binClient.close(false);
    if (firmwareStopRequested) throw error;
    appendFirmwareLog(`BIN signal mode dropped the ROM link (${error.message}); retrying with the confirmed ROM-entry state.`, 'error');
  }

  await port.close();
  portMayBeOpen = false;
  await port.open({ baudRate: 115200, dataBits: 8, parity: 'even', stopBits: 1, flowControl: 'none', bufferSize: 512 });
  portMayBeOpen = true;
  await port.setSignals({ dataTerminalReady: true, requestToSend: true });
  await delay(1000);
  await port.setSignals({ dataTerminalReady: true, requestToSend: false });
  await delay(250);
  const fallbackClient = new Stm32Bootloader(port);
  try {
    await fallbackClient.write([0x7f]);
    await fallbackClient.expectAck('ROM synchronization after BIN signal fallback', 1500);
    const info = await fallbackClient.getCommands();
    session.client = fallbackClient;
    session.info = info;
    appendFirmwareLog('ROM bootloader recovered. Continuing BIN write with the stable RTS low/DTR high state.', 'success');
    return true;
  } catch (error) {
    await fallbackClient.close(false);
    throw new Error(`Could not recover the ROM bootloader after BIN signal mode: ${error.message}`);
  }
}

async function flashSelectedFirmware() {
  if (firmwareFlashBusy) {
    firmwareStopRequested = true;
    elements.flashFirmware.disabled = true;
    elements.flashFirmware.textContent = 'Stopping…';
    appendFirmwareLog('Stop requested; waiting for the current ROM operation to finish.');
    return;
  }
  if (!selectedFirmwareImage) return;
  if (!('serial' in navigator)) {
    appendFirmwareLog('Web Serial is unavailable in this browser.', 'error');
    return;
  }
  firmwareFlashBusy = true;
  firmwareStopRequested = false;
  elements.flashLocalBuild.disabled = false;
  elements.flashLocalBuild.textContent = 'Stop';
  elements.firmwareFile.disabled = true;
  elements.clearFirmware.disabled = true;
  elements.firmwareVerify.disabled = true;
  elements.flashFirmware.disabled = false;
  elements.flashFirmware.textContent = 'Stop';
  elements.flashFirmware.classList.add('button-stop');
  elements.rebootFirmware.disabled = true;
  elements.rebootBootloader.disabled = true;
  elements.firmwareLog.textContent = '';
  appendFirmwareLog(`Starting update for ${selectedFirmwareImage.name}.`);
  let session = null;
  let success = false;
  let startupResult = 'not-checked';
  const imageIdentity = selectedFirmwareImage.identity;
  try {
    session = await openBootloaderForImage(selectedFirmwareImage.extension);
    checkFirmwareStopRequested();
    let client = session.client;
    let info = session.info;
    const deviceId = await client.getDeviceId();
    appendFirmwareLog(`Device ID: 0x${deviceId.toString(16).padStart(4, '0')}.`, 'success');
    if (deviceId !== STM32_DEVICE_ID) throw new Error(`Expected STM32L051 device ID 0x${STM32_DEVICE_ID.toString(16)}, received 0x${deviceId.toString(16)}. No flash pages were erased.`);
    const hasWrite = info.commands.includes(0x31);
    const hasErase = info.commands.includes(0x44) || info.commands.includes(0x43);
    appendFirmwareLog(`ROM commands: Write=${hasWrite}, extended erase=${info.commands.includes(0x44)}, standard erase=${info.commands.includes(0x43)}, read=${info.commands.includes(0x11)}.`);
    if (!hasWrite || !hasErase) throw new Error('ROM bootloader does not report the required erase/write commands.');
    const verify = elements.firmwareVerify.checked;
    if (verify && !info.commands.includes(0x11)) throw new Error('Verify is enabled, but this ROM bootloader does not support Read Memory.');
    let binModeFallback = false;
    if (selectedFirmwareImage.extension === 'bin') {
      binModeFallback = await applyBinSignalModeOrFallback(session);
      client = session.client;
      info = session.info;
    }
    const signalText = selectedFirmwareImage.extension === 'hex'
      ? 'RTS low, DTR high'
      : (binModeFallback ? 'RTS low, DTR high (stable fallback)' : 'RTS low, DTR low');
    appendFirmwareLog(`Signal mode: ${signalText}. ${verify ? 'Read-back verification is enabled.' : 'Read-back verification is disabled.'}`);
    setRebootStatus('Erasing only pages covered by the selected image…');
    const pages = selectedFirmwareImage.pages;
    await client.erasePages(pages.map((page) => page.number), info.commands);
    checkFirmwareStopRequested();
    appendFirmwareLog(`Erased ${pages.length} page(s) containing image data.`, 'success');
    for (let i = 0; i < pages.length; i += 1) {
      checkFirmwareStopRequested();
      const page = pages[i];
      await client.writePage(page.address, page.data);
      if (verify) {
        const actual = await client.readMemory(page.address, page.data.length);
        for (let j = 0; j < actual.length; j += 1) {
          if (actual[j] !== page.data[j]) throw new Error(`Verification mismatch at 0x${(page.address + j).toString(16)}.`);
        }
      }
      checkFirmwareStopRequested();
      if (i === pages.length - 1 || (i + 1) % Math.max(1, Math.floor(pages.length / 10)) === 0) {
        appendFirmwareLog(`${verify ? 'Programmed and verified' : 'Programmed'} ${i + 1}/${pages.length} page(s).`);
      }
    }
    checkFirmwareStopRequested();
    success = true;
    appendFirmwareLog(`Firmware update complete${verify ? '; read-back verification passed' : ''}.`, 'success');
    setRebootStatus('Firmware written. Starting the application…');
  } catch (error) {
    if (firmwareStopRequested) {
      appendFirmwareLog('Update stopped by user after the current ROM operation.', 'normal');
      setRebootStatus('Update stopped. The device remains in bootloader mode.');
    } else {
      appendFirmwareLog(`Update failed: ${error.message}`, 'error');
      setRebootStatus(`Update failed: ${error.message}`, 'error');
    }
  } finally {
    const port = session?.port || selectedSerialPort;
    if (session) {
      await session.client.close(false);
      if (success) {
        try {
          elements.flashFirmware.disabled = true;
          elements.flashFirmware.textContent = 'Starting…';
          elements.flashFirmware.classList.remove('button-stop');
          if (imageIdentity === 'ours' || imageIdentity === 'ours-partial') {
            appendFirmwareLog(imageIdentity === 'ours-partial'
              ? 'Starting partially compatible firmware and checking HELLO only…'
              : 'Starting our firmware and checking for HELLO…');
            await startFirmwareAndVerify(port);
            startupResult = imageIdentity === 'ours-partial' ? 'partial-app-verified' : 'app-verified';
          } else if (imageIdentity === 'stock') {
            appendFirmwareLog('Starting stock firmware and checking fwid in 115200 8E1…');
            const version = await startStockFirmwareAndVerify(port);
            appendFirmwareLog(`Stock firmware answered fwid: ${version}.`, 'success');
            startupResult = 'stock-verified';
          } else {
            const skipReason = imageIdentity === 'ours-incompatible'
              ? 'project image is not on the JS-compatible checksum list'
              : 'image type is unknown';
            appendFirmwareLog(`Skipping the app protocol check because the ${skipReason}; sending the firmware-start signal only.`);
            await sendFirmwareStartSignal(port);
            startupResult = 'unverified';
          }
        } catch (error) {
          startupResult = 'failed';
          const checkName = imageIdentity === 'stock' ? 'Stock fwid check' : (imageIdentity === 'ours' || imageIdentity === 'ours-partial') ? 'Application HELLO check' : 'Firmware-start signal';
          appendFirmwareLog(`${checkName} failed: ${error.message}`, 'warning');
        }
      }
    }
    const closed = await disconnectPortAfterAction(port);
    firmwareFlashBusy = false;
    firmwareStopRequested = false;
    elements.firmwareFile.disabled = false;
    elements.clearFirmware.disabled = false;
    elements.firmwareVerify.disabled = false;
    elements.rebootFirmware.disabled = false;
    elements.rebootBootloader.disabled = false;
    elements.flashFirmware.textContent = 'Flash';
    elements.flashFirmware.classList.remove('button-stop');
    elements.flashLocalBuild.disabled = false;
    elements.flashLocalBuild.textContent = 'Flash current build';
    if (success) {
      elements.firmwareFile.value = '';
      selectedFirmwareImage = null;
      appendFirmwareLog('Firmware image selection cleared.');
      if (startupResult === 'app-verified') {
        appendFirmwareLog('Application responded to HELLO. Serial port disconnected.', 'success');
        setRebootStatus('Firmware update complete; application started and serial port disconnected.', 'success');
      } else if (startupResult === 'partial-app-verified') {
        appendFirmwareLog('Partially compatible firmware answered HELLO; other protocol features may differ.', 'partial');
        setRebootStatus('Partial-compatibility firmware answered HELLO; other protocol features may differ.', 'partial');
      } else if (startupResult === 'stock-verified') {
        setRebootStatus('Firmware update complete; stock fwid response received.', 'success');
      } else if (startupResult === 'unverified') {
        const skippedType = imageIdentity === 'ours-incompatible' ? 'unlisted project firmware' : 'unknown firmware';
        appendFirmwareLog(`Startup verification skipped for ${skippedType}.`, 'warning');
        setRebootStatus(`Image programmed and verified; start signal sent. Startup check skipped for ${skippedType}.`, 'warning');
      } else if (imageIdentity === 'stock') {
        setRebootStatus('Stock image programmed and verified, but its fwid response was not confirmed.', 'warning');
      } else if (imageIdentity === 'ours') {
        setRebootStatus('Our image programmed and verified, but the application did not answer HELLO.', 'warning');
      } else if (imageIdentity === 'ours-partial') {
        setRebootStatus('Partial-compatibility image programmed and verified, but it did not answer HELLO.', 'warning');
      } else if (imageIdentity === 'ours-incompatible') {
        setRebootStatus('Our image programmed and verified, but the firmware-start signal failed.', 'warning');
      } else {
        setRebootStatus('Unknown image programmed and verified, but the firmware-start signal failed.', 'warning');
      }
    }
    elements.flashFirmware.disabled = !selectedFirmwareImage;
    if (!closed) {
      appendFirmwareLog('Port remains open; press Disconnect in the top-right to retry closing it.', 'error');
      setRebootStatus(`${elements.rebootStatus.textContent} Port is still open; press Disconnect to retry.`, 'error');
    }
  }
}

async function connectDevice(portOverride = null) {
  protocolCapabilities = 0;
  deviceSettings = null;
  deviceHiddenSettings = null;
  rateHistory.length = 0;
  logInfo = null;
  logRecords = [];
  logViewCache.clear(); logHistorical = false; logSelectedDate = ""; logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
  logStartOffset = 0;
  logPageTotal = 0;
  updateSpiDumpAvailability();
  setConnection('connecting', 'Connecting…');
  showNotice('Select the device port. The app will wait if opening it restarts the device.', '');
  try {
    serial = new SerialProtocol((frame) => serial?.dispatch(frame), (error) => {
      clearInterval(pollTimer);
      pollTimer = 0;
      serial = null;
      deviceSettings = null;
      deviceHiddenSettings = null;
      logInfo = null;
      logRecords = [];
      logViewCache.clear(); logHistorical = false; logSelectedDate = ""; logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
      logStartOffset = 0;
      logPageTotal = 0;
      elements.settingsList.replaceChildren();
      elements.logTabs.replaceChildren();
      elements.logContent.textContent = '';
      updateSpiDumpAvailability();
      elements.settingsStatus.textContent = 'Connect to read settings.';
      setConnection('disconnected', 'Connection lost');
      offlinePreview = true;
      renderOfflinePreview();
      showNotice(error.message, 'error');
    });
    await serial.connect(portOverride);
    bootloaderPortOpen = false;
    elements.rebootFirmware.disabled = false;
    setConnection('connected', 'Connected');
    showNotice('Waiting 1 second before HELLO…', '');
    const hello = await requestHelloAfterBoot(serial);
    handleHello(hello);
    offlinePreview = false;
    elements.debugModeBadge.textContent = 'DEVICE DATA';
    elements.debugModeBadge.dataset.mode = 'device';
    drawRateChart();
    showNotice('Device connected. Readings update once per second.', 'success');
    await loadSettings();
    await loadLogInfo();
    await refreshStatus();
    pollTimer = setInterval(() => {
      refreshStatus();
      refreshLatestLogRecord();
      refreshLogCounts();
    }, 1000);
  } catch (error) {
    if (serial) await serial.disconnect();
    serial = null;
    elements.rebootFirmware.disabled = false;
    deviceSettings = null;
    deviceHiddenSettings = null;
    logInfo = null;
    logRecords = [];
    logViewCache.clear(); logHistorical = false; logSelectedDate = ""; logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
    logPageTotal = 0;
    elements.settingsList.replaceChildren();
    elements.logTabs.replaceChildren();
    elements.logContent.textContent = '';
    updateSpiDumpAvailability();
    elements.settingsStatus.textContent = 'Connect to read settings.';
    setConnection(portMayBeOpen ? 'disconnect-pending' : 'disconnected', portMayBeOpen ? 'Disconnect required' : 'Disconnected');
    offlinePreview = true;
    renderOfflinePreview();
    showNotice(error.message || 'Could not connect to the device.', 'error');
  }
}

async function disconnectDevice() {
  clearInterval(pollTimer);
  pollTimer = 0;
  const closed = await disconnectPortAfterAction();
  elements.rebootFirmware.disabled = false;
  elements.rebootBootloader.disabled = false;
  polling = false;
  deviceSettings = null;
  deviceHiddenSettings = null;
  logInfo = null;
  logRecords = [];
  logViewCache.clear(); logHistorical = false; logSelectedDate = ""; logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
  logPageTotal = 0;
  elements.settingsList.replaceChildren();
  elements.logTabs.replaceChildren();
  elements.logContent.textContent = '';
  updateSpiDumpAvailability();
  elements.settingsStatus.textContent = 'Connect to read settings.';
  offlinePreview = true;
  renderOfflinePreview();
  if (!closed) showNotice('Port is still open. Press Disconnect to retry closing it.', 'error');
}

async function disconnectPortAfterAction(port = selectedSerialPort) {
  clearInterval(pollTimer);
  pollTimer = 0;
  const activeSerial = serial;
  serial = null;
  if (activeSerial) await activeSerial.disconnect();
  if (port && portMayBeOpen) {
    try {
      await port.close();
      portMayBeOpen = false;
    } catch { portMayBeOpen = true; }
  }
  bootloaderPortOpen = false;
  const stillOpen = portMayBeOpen;
  setConnection(stillOpen ? 'disconnect-pending' : 'disconnected', stillOpen ? 'Disconnect required' : 'Disconnected');
  return !stillOpen;
}

async function sendFirmwareStartSignal(port) {
  if (serial) {
    await serial.disconnect();
    serial = null;
  }
  if (!portMayBeOpen) {
    await port.open({ baudRate: 115200, dataBits: 8, parity: 'none', stopBits: 1, flowControl: 'none', bufferSize: 256 });
    portMayBeOpen = true;
  }
  await port.setSignals({ dataTerminalReady: false, requestToSend: true });
  await delay(1000);
  await port.close();
  portMayBeOpen = false;
}

async function startFirmwareAndVerify(port) {
  await sendFirmwareStartSignal(port);
  let probe = null;
  try {
    probe = new SerialProtocol((frame) => probe?.dispatch(frame), () => {});
    await probe.connect(port);
    await requestHelloAfterBoot(probe);
  } finally {
    if (probe) await probe.disconnect();
  }
}

async function startStockFirmwareAndVerify(port) {
  await sendFirmwareStartSignal(port);
  await port.open({ baudRate: 115200, dataBits: 8, parity: 'even', stopBits: 1, flowControl: 'none', bufferSize: 256 });
  portMayBeOpen = true;
  await delay(2000);

  const writer = port.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode('fwid'));
  } finally {
    writer.releaseLock();
  }

  const reader = port.readable.getReader();
  const response = [];
  try {
    while (response.length < 6) {
      const result = await Promise.race([
        reader.read(),
        delay(3000).then(() => { throw new Error('Timed out waiting for the stock fwid response.'); }),
      ]);
      if (result.done) throw new Error('Serial port closed before the stock fwid response arrived.');
      response.push(...result.value);
    }
  } finally {
    try { await reader.cancel(); } catch { /* stream may already be closed */ }
    try { reader.releaseLock(); } catch { /* already released */ }
  }
  const version = new TextDecoder().decode(Uint8Array.from(response.slice(0, 6)));
  if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error(`Unexpected stock fwid response: ${JSON.stringify(version)}.`);
  return version;
}

async function rebootToFirmware() {
  if (elements.rebootFirmware.disabled) return;
  elements.rebootFirmware.disabled = true;
  elements.rebootBootloader.disabled = true;
  setRebootStatus('Starting firmware and checking for HELLO…');
  appendFirmwareLog('Starting firmware and checking for HELLO.');
  showNotice('Sending the firmware-start signal and checking that the application responds.', '');
  clearInterval(pollTimer);
  pollTimer = 0;
  setConnection('connecting', 'Starting firmware…');
  let port = selectedSerialPort;
  let actionError = null;
  try {
    if (!port) {
      if (!('serial' in navigator)) throw new Error('Web Serial is unavailable in this browser.');
      port = await navigator.serial.requestPort();
      selectedSerialPort = port;
    }

    await startFirmwareAndVerify(port);
    setRebootStatus('Firmware is running. Serial port disconnected.', 'success');
    appendFirmwareLog('Firmware responded to HELLO; serial port disconnected.', 'success');
    showNotice('Firmware responded to HELLO. Serial port disconnected.', 'success');
  } catch (error) {
    actionError = error;
    const level = error.code === 'APP_HELLO_NO_RESPONSE' ? 'warning' : 'error';
    setRebootStatus(`Firmware did not respond: ${error.message}`, level);
    appendFirmwareLog(`Firmware start failed: ${error.message}`, level);
    showNotice(`Firmware did not respond: ${error.message}`, level);
  } finally {
    const closed = await disconnectPortAfterAction(port);
    if (!closed) {
      const message = 'Serial port is still open. Press Disconnect in the top-right to retry.';
      setRebootStatus(`${elements.rebootStatus.textContent} ${message}`, 'error');
      appendFirmwareLog(message, 'error');
      showNotice(message, 'error');
    } else if (actionError) {
      setConnection('disconnected', 'Disconnected');
    }
    elements.rebootFirmware.disabled = false;
    elements.rebootBootloader.disabled = false;
  }
}

async function rebootToBootloader() {
  elements.rebootBootloader.disabled = true;
  elements.rebootFirmware.disabled = true;
  let port = selectedSerialPort;
  let actionError = null;
  appendFirmwareLog('Starting ROM bootloader entry check.');
  try {
    if (!port) {
      if (!('serial' in navigator)) throw new Error('Web Serial is unavailable in this browser.');
      port = await navigator.serial.requestPort();
      selectedSerialPort = port;
    }
    setRebootStatus('Trying the full updater sequence in 115200 8E1…');
    showNotice('Bootloader test: RTS/DTR high-high, then RTS low with DTR high, then ROM sync.', '');
    clearInterval(pollTimer);
    pollTimer = 0;
    if (serial) await serial.disconnect();
    serial = null;
    if (bootloaderPortOpen) {
      try { await port.close(); portMayBeOpen = false; } catch { portMayBeOpen = true; }
      bootloaderPortOpen = false;
    }
    setConnection('connecting', 'Testing bootloader…');
    await port.open({ baudRate: 115200, dataBits: 8, parity: 'even', stopBits: 1, flowControl: 'none', bufferSize: 64 });
    portMayBeOpen = true;
    await port.setSignals({ dataTerminalReady: true, requestToSend: true });
    await delay(1000);
    await port.setSignals({ dataTerminalReady: true, requestToSend: false });
    await delay(250);
    const writer = port.writable.getWriter();
    try { await writer.write(Uint8Array.of(0x7f)); }
    finally { writer.releaseLock(); }
    const reader = port.readable.getReader();
    let response;
    try {
      response = await Promise.race([
        reader.read(),
        delay(1200).then(() => { throw new Error('No ROM ACK (0x79) received.'); }),
      ]);
    } finally {
      if (!response) {
        try { await reader.cancel(); } catch { /* stream may have ended */ }
      }
      try { reader.releaseLock(); } catch { /* already released */ }
    }
    if (response.done || !response.value?.includes(0x79)) {
      throw new Error('No ROM ACK (0x79) received.');
    }
    setRebootStatus('ROM bootloader replied with ACK. Port disconnected; Flash can reconnect to it.', 'success');
    appendFirmwareLog('ROM bootloader replied with ACK; port disconnected.', 'success');
    showNotice('STM32 ROM bootloader detected. No firmware data was sent.', 'success');
  } catch (error) {
    actionError = error;
    setRebootStatus(error.message, 'error');
    appendFirmwareLog(`Bootloader entry failed: ${error.message}`, 'error');
    showNotice(error.message, 'error');
  } finally {
    const closed = await disconnectPortAfterAction(port);
    if (!closed) {
      const message = 'Serial port is still open. Press Disconnect in the top-right to retry.';
      setRebootStatus(`${elements.rebootStatus.textContent} ${message}`, 'error');
      appendFirmwareLog(message, 'error');
      showNotice(message, 'error');
    } else if (actionError) {
      setConnection('disconnected', 'Bootloader not detected');
    }
    elements.rebootBootloader.disabled = false;
    elements.rebootFirmware.disabled = false;
  }
}

elements.connect.addEventListener('click', () => {
  if (serial || bootloaderPortOpen || portMayBeOpen) disconnectDevice();
  else connectDevice();
});

document.querySelectorAll('.debug-section-toggle').forEach((button) => {
  button.addEventListener('click', () => {
    const expanded = button.getAttribute('aria-expanded') === 'true';
    button.setAttribute('aria-expanded', String(!expanded));
    document.querySelectorAll(`[data-debug-group="${button.getAttribute('aria-controls')}"]`)
      .forEach((row) => { row.hidden = expanded; });
  });
});
elements.rateChart.addEventListener('pointermove', showRateChartTooltip);
elements.rateChart.addEventListener('pointerleave', () => {
  hideRateChartTooltip();
  drawRateChart();
});
renderOfflinePreview();
renderSettingsClock();
window.setInterval(renderSettingsClock, 1000);
if (!('serial' in navigator)) {
  elements.connect.disabled = true;
  showNotice('Web Serial is not supported. Use a supported browser on localhost or HTTPS.', 'error');
}
