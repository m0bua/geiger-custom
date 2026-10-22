const VERSION = 1;
const KIND_REQUEST = 1;
const KIND_RESPONSE = 2;
const OP_HELLO = 1;
const OP_GET_STATUS = 2;
const OP_GET_SETTINGS = 3;
const OP_SET_SETTING = 4;
const OP_GET_LOG_INFO = 5;
const OP_GET_LOG_PAGE = 6;
const CAP_GET_SETTINGS = 1 << 1;
const CAP_SET_SETTING = 1 << 2;
const MAX_PAYLOAD = 64;
const LOG_BATCH_SIZE = 6;

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

const elements = {
  connect: document.querySelector('#connect-button'),
  connection: document.querySelector('#connection'),
  connectionLabel: document.querySelector('#connection-label'),
  notice: document.querySelector('#notice'),
  rateChart: document.querySelector('#rate-chart'),
  deviceDate: document.querySelector('#device-date'),
  deviceTime: document.querySelector('#device-time'),
  overviewUptime: document.querySelector('#overview-uptime'),
  solarPercent: document.querySelector('#solar-percent'),
  batteryPercent: document.querySelector('#battery-percent'),
  overviewTab: document.querySelector('#overview-tab'),
  debugTab: document.querySelector('#debug-tab'),
  settingsTab: document.querySelector('#settings-tab'),
  logsTab: document.querySelector('#logs-tab'),
  firmwareTab: document.querySelector('#firmware-tab'),
  instructionsTab: document.querySelector('#instructions-tab'),
  overviewPage: document.querySelector('#overview-page'),
  debugPage: document.querySelector('#debug-page'),
  settingsPage: document.querySelector('#settings-page'),
  logsPage: document.querySelector('#logs-page'),
  logTabs: document.querySelector('#log-tabs'),
  logContent: document.querySelector('#log-content'),
  logDate: document.querySelector('#log-date'),
  loadLogDate: document.querySelector('#load-log-date'),
  logsStatus: document.querySelector('#logs-status'),
  loadMoreLogs: document.querySelector('#load-more-logs'),
  firmwarePage: document.querySelector('#firmware-page'),
  instructionsPage: document.querySelector('#instructions-page'),
  settingsStatus: document.querySelector('#settings-status'),
  settingsList: document.querySelector('#settings-list'),
  rate: document.querySelector('#rate'),
  rateFoot: document.querySelector('#rate-foot'),
  rateLabel: document.querySelector('#rate-label'),
  rateDose: document.querySelector('#rate-dose'),
  rateUnit: document.querySelector('#rate-unit'),
  dose: document.querySelector('#dose'),
  doseUnit: document.querySelector('#dose-unit'),
  count: document.querySelector('#count'),
  doseFoot: document.querySelector('#dose-foot'),
  uptime: document.querySelector('#uptime'),
  debugBattery: document.querySelector('#debug-battery'),
  debugSolar: document.querySelector('#debug-solar'),
  debugUsb: document.querySelector('#debug-usb'),
  debugCount: document.querySelector('#debug-count'),
  debugRtc: document.querySelector('#debug-rtc'),
  debugClock: document.querySelector('#debug-clock'),
  debugMinutes: document.querySelector('#debug-minutes'),
  debugHours: document.querySelector('#debug-hours'),
  debugDays: document.querySelector('#debug-days'),
  debugAlerts: document.querySelector('#debug-alerts'),
  memorySpi: document.querySelector('#memory-spi'),
  memoryJedec: document.querySelector('#memory-jedec'),
  debugHeading: document.querySelector('#debug-heading'),
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

  async connect() {
    if (!('serial' in navigator)) {
      throw new Error('Web Serial is not available in this browser. Try a supported desktop browser.');
    }
    this.port = await navigator.serial.requestPort();
    this.portInfo = this.port.getInfo?.() || {};
    await this.port.open({ baudRate: 115200, dataBits: 8, parity: 'none', stopBits: 1, flowControl: 'none', bufferSize: 256 });
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
      try { await this.port.close(); } catch { /* already closed */ }
      this.port = null;
    }
    this.readTask = null;
  }
}

let serial = null;
let pollTimer = 0;
let polling = false;
let protocolCapabilities = 0;
let deviceSettings = null;
let logInfo = null;
let selectedLogType = 'days';
let logRecords = [];
let logStartOffset = 0;
let selectedDebugPage = 'main';
let rateHistoryUnit = 'CPM';
let rateLevel = 'blue';
let logPageTotal = 0;
let logPageLoading = false;
let logPageReloadPending = false;
let logPageGeneration = 0;
let logInfoLoading = false;
const rateHistory = [];

const remoteSettings = [
  { id: 0, label: 'LED', options: ['OFF', 'ON', 'ALWAYS', 'YELLOW', 'RED'] },
  { id: 1, label: 'CLICKS', options: ['OFF', 'ON', 'ALWAYS', 'YELLOW', 'RED'] },
  { id: 2, label: 'SCREEN ALARM', options: ['OFF', 'YELLOW', 'RED'] },
  { id: 3, label: 'LIGHT ALARM', options: ['OFF', 'YELLOW', 'RED'] },
  { id: 4, label: 'SOUND ALARM', options: ['OFF', 'YELLOW', 'RED'] },
  { id: 5, label: 'LED BRIGHT', options: ['AUTO', '1%', '3%', '5%', '10%', '25%', '50%', '80%', '100%'] },
  { id: 6, label: 'SCREEN BRIGHT', options: ['AUTO', '1%', '3%', '5%', '10%', '25%', '50%', '80%', '100%'] },
  { id: 7, label: 'SCREEN TIMER', options: ['1 s', '5 s', '10 s', '30 s', '60 s', '90 s'] },
  { id: 8, label: 'LEAD ZERO', options: ['SHOW', 'HIDE'] },
  { id: 9, label: 'UNIT', options: ['Sv', 'CNT', 'R'] },
  { id: 10, label: 'RATE PREFIX', options: ['m', 'µ', 'n', 'AUTO'] },
  { id: 11, label: 'DOSE PREFIX', options: ['m', 'µ', 'n', 'AUTO'] },
  { id: 13, label: 'USE-SPI', options: ['OFF', 'ON'] },
  { id: 15, label: 'HV', options: ['ON', 'OFF'] },
  { id: 16, label: 'DOSE ALARM', options: ['OFF', 'AUTO-Y', 'AUTO-R', '5', '7', '10', '30', '50', '100', '500', '1000'] },
  { id: 17, label: 'ALARM STOP', options: ['CLEAR', 'BUTTON'] },
  { id: 20, label: 'TONE', options: ['STOCK', 'CLASSIC', 'MODERN'] },
];

function percentFromAdc(raw, empty, full) {
  if (raw <= empty) return 0;
  if (raw >= full) return 100;
  return 100 - Math.floor(((full - raw) * 100) / (full - empty));
}

function formatRadiation(valueUsv, prefixSetting, symbol, perHour) {
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
  const digits = value === 0 ? 0 : Math.max(0,
    Math.min(maxDecimals, 5 - integerDigits));
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
  if (rateHistory.length < 2) return;

  const levelColors = {
    blue: '#287bff', green: '#00e58b', yellow: '#f0d35e', red: '#ff6666',
  };
  const levelColor = levelColors[rateLevel];

  const peak = Math.max(...rateHistory, 0.01) * 1.12;
  const points = rateHistory.map((value, index) => ({
    x: (index / (rateHistory.length - 1)) * w,
    y: h - (value / peak) * h,
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
}

function showNotice(message, kind = '') {
  elements.notice.dataset.kind = kind;
  elements.notice.querySelector('p').textContent = message;
}

function selectPage(page) {
  const pages = ['overview', 'debug', 'settings', 'logs', 'instructions', 'firmware'];
  for (const name of pages) {
    const active = name === page;
    elements[`${name}Page`].hidden = !active;
    elements[`${name}Tab`].classList.toggle('active', active);
    elements[`${name}Tab`].setAttribute('aria-selected', String(active));
  }
  if (page === 'overview') drawRateChart();
  if (page === 'settings' && serial && deviceSettings === null) loadSettings();
  if (page === 'logs' && serial && logInfo === null) loadLogInfo();
  else if (page === 'logs' && serial && logInfo && logRecords.length === 0) loadLogPage(true);
}

function selectDebugPage(name) {
  selectedDebugPage = name;
  elements.debugHeading.textContent = {
    main: 'DEBUG', memory: 'DEBUG MEM', logs: 'DEBUG LOGS', voltage: 'DEBUG VOLT',
  }[name];
  document.querySelectorAll('[data-debug-panel]').forEach((panel) => {
    panel.hidden = panel.dataset.debugPanel !== name;
  });
  document.querySelectorAll('[data-debug-page]').forEach((button) => {
    const active = button.dataset.debugPage === name;
    button.classList.toggle('active', active);
    button.setAttribute('aria-selected', String(active));
  });
}

function renderDebugLogInfo() {
  if (!logInfo) return;
  elements.debugRtc.textContent = logInfo.rtcStatus ||
    (logInfo.calendarValid ? 'SET' : 'NOT SET');
  elements.debugClock.textContent = logInfo.clockSource || '—';
  elements.debugMinutes.textContent = logInfo.minutes.toLocaleString('en-US');
  elements.debugHours.textContent = logInfo.hours.toLocaleString('en-US');
  elements.debugDays.textContent = logInfo.days.toLocaleString('en-US');
  elements.debugAlerts.textContent = logInfo.alerts.toLocaleString('en-US');
  elements.memorySpi.textContent = logInfo.spiSizeMb === null ? '—' :
    `${logInfo.spiSizeMb}MB`;
  elements.memorySpi.classList.toggle('state-error', logInfo.spi && !logInfo.spiReady);
  elements.memoryJedec.textContent = logInfo.jedec ? logInfo.jedec : '—';
}

const logTypes = [
  { id: 'days', label: 'DAYS', countKey: 'days' },
  { id: 'minutes', label: 'MINUTES', countKey: 'minutes', spi: true },
  { id: 'hours', label: 'HOURS', countKey: 'hours', spi: true },
  { id: 'alerts', label: 'ALERTS', countKey: 'alerts', spi: true },
];

function renderLogTabs() {
  elements.logTabs.replaceChildren();
  if (!logInfo) return;
  const enabled = logTypes.filter((type) => !type.spi || logInfo.spi);
  if (!enabled.some((type) => type.id === selectedLogType)) selectedLogType = 'days';
  for (const type of enabled) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `log-tab${type.id === selectedLogType ? ' active' : ''}`;
    button.textContent = `${type.label} (${logInfo[type.countKey].toLocaleString('en-US')})`;
    button.setAttribute('aria-selected', String(type.id === selectedLogType));
    button.addEventListener('click', () => {
      selectedLogType = type.id;
      logRecords = [];
      logPageTotal = 0;
      logStartOffset = 0;
      renderLogTabs();
      loadLogPage(true);
    });
    elements.logTabs.append(button);
  }
  renderLogSummary();
}

function renderLogSummary() {
  elements.logContent.replaceChildren();
  elements.logDate.disabled = !logInfo || selectedLogType === 'alerts';
  elements.loadLogDate.disabled = elements.logDate.disabled;
  elements.loadMoreLogs.hidden = !logInfo || logPageLoading ||
    logStartOffset + logRecords.length >= logPageTotal || logPageTotal === 0;
  if (!logInfo) return;
  const type = logTypes.find((item) => item.id === selectedLogType) || logTypes[0];
  if (logPageTotal === 0) {
    elements.logContent.textContent = 'No records stored.';
    return;
  }
  elements.logsStatus.textContent = `${(logStartOffset + logRecords.length).toLocaleString('en-US')} of ${logPageTotal.toLocaleString('en-US')} records`;
  const maxDose = Math.max(1, logRecords.reduce((maximum, record) =>
    Math.max(maximum, record.dose), 0));
  const header = document.createElement('div');
  header.className = 'log-grid log-grid-header';
  header.innerHTML = selectedLogType === 'alerts' ?
    '<span>Date</span><span>Duration</span><span>Dose</span><span>Average</span><span>Maximum</span>' :
    '<span>Date</span><span>Dose</span><span>Minimum</span><span>Average</span><span>Maximum</span>';
  elements.logContent.append(header);
  logRecords.forEach((record, index) => {
    const row = document.createElement('div');
    const severity = record.maximum >= 136 ? 'red' : (record.maximum >= 61 ? 'yellow' : 'normal');
    row.className = `log-grid log-record severity-${severity}`;
    row.style.setProperty('--bar-width', `${Math.min(100, record.dose * 100 / maxDose)}%`);
    const cells = [formatLogDate(record.timestamp, selectedLogType),
      ...logValueCells(record, logStartOffset + index)];
    cells.forEach((value) => {
      const cell = document.createElement('span');
      cell.textContent = value;
      row.append(cell);
    });
    elements.logContent.append(row);
  });
  elements.loadMoreLogs.hidden = logPageLoading || logStartOffset + logRecords.length >= logPageTotal;
}

function logValueCells(record, index) {
  const isCount = (deviceSettings?.[9] ?? 0) === 1;
  if (selectedLogType === 'alerts') {
    const seconds = record.duration;
    const duration = seconds < 600 ? `${seconds}s` :
      (seconds < 3600 ? `${Math.max(1, Math.round(seconds / 60))}m` :
        `${(seconds / 3600).toFixed(1)}h`);
    const averageCpm = Math.round(record.dose * 60 / Math.max(1, seconds));
    if (isCount) return [duration, `${record.dose.toLocaleString('en-US')} CNT`,
      `${averageCpm} CPM`, `${record.maximum} CPM`];
    const dose = formatLogRadiation(record.dose * 0.000111);
    const average = formatLogRadiation(averageCpm * 0.00666, true);
    const maximum = formatLogRadiation(record.maximum * 0.00666, true);
    return [duration, `${dose.value} ${dose.unit}`, `${average.value} ${average.unit}`,
      `${maximum.value} ${maximum.unit}`];
  }
  const periodSeconds = selectedLogType === 'minutes' ? 60 :
    (selectedLogType === 'hours' ? 3600 : 86400);
  const seconds = index === 0 && logInfo?.calendarValid ?
    (logInfo.calendarSeconds % periodSeconds || periodSeconds) : periodSeconds;
  const averageCpm = Math.round(record.dose * 60 / seconds);
  if (isCount) return [`${record.dose.toLocaleString('en-US')} CNT`,
    `${record.minimum} CPM`, `${averageCpm} CPM`, `${record.maximum} CPM`];
  const dose = formatLogRadiation(record.dose * 0.000111);
  const minimum = formatLogRadiation(record.minimum * 0.00666, true);
  const average = formatLogRadiation(averageCpm * 0.00666, true);
  const maximum = formatLogRadiation(record.maximum * 0.00666, true);
  return [`${dose.value} ${dose.unit}`, `${minimum.value} ${minimum.unit}`,
    `${average.value} ${average.unit}`, `${maximum.value} ${maximum.unit}`];
}

function loadLogNearDate() {
  if (!logInfo?.calendarValid || !elements.logDate.value || selectedLogType === 'alerts') return;
  const dateSeconds = new Date(`${elements.logDate.value}T00:00:00Z`).getTime() / 1000;
  const periodSeconds = selectedLogType === 'minutes' ? 60 :
    (selectedLogType === 'hours' ? 3600 : 86400);
  const physicalCount = selectedLogType === 'minutes' ? logInfo.minutes :
    (selectedLogType === 'hours' ? logInfo.hours : logInfo.days);
  const total = physicalCount + 1;
  logStartOffset = Math.min(total - 1, Math.max(0,
    Math.floor((logInfo.calendarSeconds - dateSeconds) / periodSeconds)));
  logRecords = [];
  logPageTotal = 0;
  loadLogPage(true);
}

function formatLogDate(seconds, type) {
  if (!seconds) return 'Time unavailable';
  const date = new Date(Date.UTC(2000, 0, 1) + seconds * 1000);
  const datePart = [String(date.getUTCFullYear()).slice(-2),
    String(date.getUTCMonth() + 1).padStart(2, '0'),
    String(date.getUTCDate()).padStart(2, '0')].join('-');
  if (type === 'days') return datePart;
  const timePart = `${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}`;
  return type === 'hours' ? `${datePart} ${timePart.slice(0, 2)}h` : `${datePart} ${timePart}`;
}

function formatLogRadiation(valueUsv, isRate = false) {
  const prefixSetting = deviceSettings?.[isRate ? 10 : 11] ?? 1;
  const symbol = (deviceSettings?.[9] ?? 0) === 2 ? 'R' : 'Sv';
  return formatRadiation(valueUsv * (symbol === 'R' ? 100 : 1), prefixSetting,
    symbol, isRate);
}

function formatAggregateCells(record, index) {
  const periodSeconds = selectedLogType === 'minutes' ? 60 :
    (selectedLogType === 'hours' ? 3600 : 86400);
  let seconds = periodSeconds;
  if (index === 0 && logInfo?.calendarValid) {
    seconds = logInfo.calendarSeconds % periodSeconds || periodSeconds;
  }
  const averageCpm = Math.round(record.dose * 60 / seconds);
  if ((deviceSettings?.[9] ?? 0) === 1) {
    return `D ${record.dose.toLocaleString('en-US')} CNT · MIN ${record.minimum} · AVG ${averageCpm} · MAX ${record.maximum} CPM`;
  }
  const dose = formatLogRadiation(record.dose * 0.000111);
  const minimum = formatLogRadiation(record.minimum * 0.00666, true);
  const maximum = formatLogRadiation(record.maximum * 0.00666, true);
  const average = formatLogRadiation(averageCpm * 0.00666, true);
  return `D ${dose.value} ${dose.unit} · MIN ${minimum.value} · AVG ${average.value} · MAX ${maximum.value}`;
}

function formatAlertCells(record) {
  const seconds = record.duration;
  const duration = seconds < 600 ? `${seconds}s` :
    (seconds < 3600 ? `${Math.max(1, Math.round(seconds / 60))}m` :
      `${(seconds / 3600).toFixed(1)}h`);
  if ((deviceSettings?.[9] ?? 0) === 1) {
    const avg = Math.round(record.dose * 60 / Math.max(1, seconds));
    return `${duration} · D ${record.dose.toLocaleString('en-US')} CNT · AVG ${avg} · MAX ${record.maximum} CPM`;
  }
  const dose = formatLogRadiation(record.dose * 0.000111);
  const maximum = formatLogRadiation(record.maximum * 0.00666, true);
  const average = formatLogRadiation(record.dose * 60 / Math.max(1, seconds) * 0.00666, true);
  return `${duration} · D ${dose.value} ${dose.unit} · AVG ${average.value} · MAX ${maximum.value}`;
}

async function loadLogPage(reset = false) {
  if (!serial || !logInfo) return;
  if (logPageLoading) {
    if (reset) {
      logPageGeneration += 1;
      logPageReloadPending = true;
    }
    return;
  }
  const type = logTypes.find((item) => item.id === selectedLogType) || logTypes[0];
  if (reset) {
    logPageGeneration += 1;
    logRecords = [];
    logPageTotal = 0;
  }
  const generation = logPageGeneration;
  logPageLoading = true;
  elements.loadMoreLogs.disabled = true;
  elements.logsStatus.textContent = 'Loading records…';
  renderLogSummary();
  const typeId = logTypes.indexOf(type);
  const target = logRecords.length + LOG_BATCH_SIZE;
  let failure = '';
  try {
    while (logRecords.length < target &&
        (logStartOffset + logRecords.length < logPageTotal || logPageTotal === 0)) {
      const offset = logStartOffset + logRecords.length;
      const payload = await serial.request(OP_GET_LOG_PAGE,
        Uint8Array.of(typeId, offset & 0xff, (offset >> 8) & 0xff, 3), 5000);
      if (generation !== logPageGeneration) break;
      if (payload.length < 7 || payload[0] !== 0 || payload[1] !== typeId) {
        throw new ProtocolError('Invalid log page response.');
      }
      const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      const responseOffset = view.getUint16(2, true);
      const total = view.getUint16(4, true);
      const count = payload[6];
      if (responseOffset !== offset || payload.length !== 7 + count * 16) {
        throw new ProtocolError('Log page is incomplete or out of sequence.');
      }
      logPageTotal = total;
      for (let i = 0; i < count; i += 1) {
        const at = 7 + i * 16;
        const timestamp = view.getUint32(at, true);
        if (typeId === 3) {
          logRecords.push({
            timestamp,
            duration: view.getUint32(at + 4, true),
            dose: view.getUint32(at + 8, true),
            maximum: view.getUint32(at + 12, true),
          });
        } else {
          logRecords.push({
            timestamp,
            dose: view.getUint32(at + 4, true),
            minimum: view.getUint32(at + 8, true),
            maximum: view.getUint32(at + 12, true),
          });
        }
      }
      if (count < 3) {
        logPageTotal = offset + count;
        break;
      }
    }
  } catch (error) {
    failure = error.message;
  } finally {
    logPageLoading = false;
    elements.loadMoreLogs.disabled = false;
    renderLogSummary();
    if (failure) elements.logsStatus.textContent = failure;
    if (logPageReloadPending && serial && logInfo) {
      logPageReloadPending = false;
      loadLogPage(true);
    }
  }
}

async function loadLogInfo() {
  if (!serial || logInfoLoading) return;
  if ((protocolCapabilities & (1 << 3)) === 0 ||
      (protocolCapabilities & (1 << 4)) === 0) {
    elements.logsStatus.textContent = 'Firmware log transfer is not supported.';
    return;
  }
  logInfoLoading = true;
  elements.logsStatus.textContent = 'Reading log index…';
  try {
    const payload = await serial.request(OP_GET_LOG_INFO);
    if (![15, 22, 26].includes(payload.length) || payload[0] !== 0) {
      throw new ProtocolError('Invalid log index response.');
    }
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    logInfo = {
      spi: payload[1] !== 0,
      minutes: view.getUint16(2, true),
      hours: view.getUint16(4, true),
      days: view.getUint16(6, true),
      alerts: view.getUint16(8, true),
      calendarValid: payload[10] !== 0,
      calendarSeconds: view.getUint32(11, true),
      calendarAnchorSeconds: view.getUint32(11, true),
      calendarAnchorUptime: payload.length >= 26 ? view.getUint32(22, true) : null,
      spiSizeMb: payload.length >= 22 ? payload[15] : null,
      spiReady: payload.length >= 22 && payload[16] !== 0,
      jedec: payload.length >= 22 ? Array.from(payload.subarray(17, 20),
        (byte) => byte.toString(16).padStart(2, '0')).join(' ').toUpperCase() : '',
      rtcStatus: payload.length >= 22 ? ({
        1: 'OSC ERR', 2: 'INIT ERR', 3: 'SYNC ERR', 4: 'DBP ERR',
        5: 'SET', 6: 'NOT SET', 7: 'NOT READY',
      }[payload[20]] || 'UNKNOWN') : '',
      clockSource: payload.length >= 22 ? (payload[21] === 1 ? 'LSE' : 'LSI') : '',
    };
    renderDebugLogInfo();
    if (logInfo.calendarValid) {
      elements.logDate.value = new Date(logInfo.calendarSeconds * 1000)
        .toISOString().slice(0, 10);
    }
    updateDeviceDateTime();
    selectedLogType = 'days';
    renderLogTabs();
    elements.logsStatus.textContent = 'Physical records on device';
    if (!logInfo.spi) {
      elements.logsStatus.textContent = 'EEPROM · day history';
    }
    if (!elements.logsPage.hidden) loadLogPage(true);
  } catch (error) {
    elements.logsStatus.textContent = error.message;
  } finally {
    logInfoLoading = false;
  }
}

function setConnection(state, label) {
  elements.connection.dataset.state = state;
  elements.connectionLabel.textContent = label;
  elements.connect.disabled = state === 'connecting';
  elements.connect.textContent = state === 'connected' ? 'Disconnect' :
    (state === 'connecting' ? 'Connecting…' : 'Connect');
  elements.connect.classList.toggle('button-primary', state !== 'connected');
  elements.connect.classList.toggle('button-quiet', state === 'connected');
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

function updateDeviceDateTime(uptimeText = null) {
  if (!logInfo?.calendarValid) {
    elements.deviceDate.textContent = '—';
    elements.deviceTime.textContent = '—';
  } else {
    const date = new Date(Date.UTC(2000, 0, 1) + logInfo.calendarSeconds * 1000);
    elements.deviceDate.textContent = [date.getUTCFullYear(),
      String(date.getUTCMonth() + 1).padStart(2, '0'),
      String(date.getUTCDate()).padStart(2, '0')].join('-');
    elements.deviceTime.textContent = `${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}`;
  }
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
}

function settingsOptions(setting) {
  if ((setting.id === 10 || setting.id === 11) && deviceSettings?.[9] === 1) {
    return ['CPM', 'CPS'];
  }
  return setting.options;
}

function renderSettings() {
  elements.settingsList.replaceChildren();
  if (!deviceSettings) return;
  for (const setting of remoteSettings) {
    if (setting.id === 11 && deviceSettings[9] === 1) continue;
    const options = settingsOptions(setting);
    const row = document.createElement('div');
    row.className = 'setting-row';
    const label = document.createElement('label');
    label.htmlFor = `setting-${setting.id}`;
    label.textContent = setting.label;
    const select = document.createElement('select');
    select.id = `setting-${setting.id}`;
    select.dataset.settingId = String(setting.id);
    options.forEach((text, index) => {
      const option = document.createElement('option');
      option.value = String(index);
      option.textContent = text;
      select.append(option);
    });
    select.value = String(deviceSettings[setting.id]);
    select.addEventListener('change', () => writeSetting(setting.id, Number(select.value), select));
    row.append(label, select);
    elements.settingsList.append(row);
  }
}

async function loadSettings() {
  if (!serial) return;
  if ((protocolCapabilities & (CAP_GET_SETTINGS | CAP_SET_SETTING)) !==
      (CAP_GET_SETTINGS | CAP_SET_SETTING)) {
    elements.settingsStatus.textContent = 'Firmware settings transfer is not supported.';
    return;
  }
  elements.settingsStatus.textContent = 'Reading…';
  try {
    const payload = await serial.request(OP_GET_SETTINGS);
    if (payload.length < 2 || payload[0] !== 0 || payload[1] !== 21 || payload.length !== 23) {
      throw new ProtocolError('Invalid settings response.');
    }
    deviceSettings = Array.from(payload.subarray(2));
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
    const result = await serial.request(OP_SET_SETTING, Uint8Array.of(settingId, value));
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

function handleStatus(payload) {
  if ((payload.length !== 25 && payload.length !== 29) || payload[0] !== 0) throw new ProtocolError('Device rejected GET_STATUS or returned an unknown format.');
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const uptimeSeconds = view.getUint32(1, true);
  const count = view.getUint32(5, true);
  const cpm = view.getUint32(9, true);
  const doseNsv = view.getUint32(13, true);
  const batteryRaw = view.getUint16(17, true);
  const solarRaw = view.getUint16(19, true);
  const usbRaw = view.getUint16(21, true);
  const countTotal = payload.length >= 29 ? view.getUint32(25, true) : count;
  rateLevel = countTotal < 25 ? 'blue' :
    (cpm * 666 > 90000 ? 'red' : (cpm * 666 >= 40000 ? 'yellow' : 'green'));
  const levelClass = `level-${rateLevel}`;
  elements.rateDose.classList.remove('level-blue', 'level-green', 'level-yellow', 'level-red');
  elements.rateDose.classList.add(levelClass);
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

  if (unit === 1) {
    elements.rate.textContent = cpm.toLocaleString('en-US');
    elements.rateLabel.textContent = 'RATE';
    elements.rateDose.textContent = cpm.toLocaleString('en-US');
    elements.rateUnit.textContent = 'CPM';
    elements.dose.textContent = count.toLocaleString('en-US');
    elements.doseUnit.textContent = 'CNT';
    elements.rateFoot.hidden = true;
    elements.doseFoot.hidden = true;
  } else {
    const symbol = unit === 2 ? 'R' : 'Sv';
    const rate = formatRadiation(rateUsvh, ratePrefix, symbol, true);
    const dose = formatRadiation(doseUsv, dosePrefix, symbol, false);
    elements.rateLabel.textContent = 'RATE';
    elements.rateDose.textContent = rate.value;
    elements.rateUnit.textContent = rate.unit;
    elements.dose.textContent = dose.value;
    elements.doseUnit.textContent = dose.unit;
    elements.rateFoot.hidden = true;
    elements.doseFoot.hidden = true;
  }
  elements.count.textContent = count.toLocaleString('en-US');
  elements.debugCount.textContent = countTotal.toLocaleString('en-US');
  elements.uptime.textContent = `${uptime.days}D ${uptime.clock.slice(0, 5)}`;
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
  elements.debugBattery.textContent = `${batteryPercent}% · ${batteryRaw}`;
  elements.debugSolar.textContent = `${solarPercent}% · ${solarRaw}`;
  elements.debugUsb.textContent = usbRaw.toLocaleString('en-US');
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
  throw lastError;
}

elements.overviewTab.addEventListener('click', () => selectPage('overview'));
elements.debugTab.addEventListener('click', () => selectPage('debug'));
elements.settingsTab.addEventListener('click', () => selectPage('settings'));
elements.logsTab.addEventListener('click', () => selectPage('logs'));
elements.loadMoreLogs.addEventListener('click', () => loadLogPage(false));
elements.loadLogDate.addEventListener('click', loadLogNearDate);
elements.firmwareTab.addEventListener('click', () => selectPage('firmware'));
elements.instructionsTab.addEventListener('click', () => selectPage('instructions'));
document.querySelectorAll('[data-debug-page]').forEach((button) => {
  button.addEventListener('click', () => selectDebugPage(button.dataset.debugPage));
});
selectDebugPage(selectedDebugPage);
window.addEventListener('resize', drawRateChart);

async function connectDevice() {
  protocolCapabilities = 0;
  deviceSettings = null;
  logInfo = null;
  logRecords = [];
  logStartOffset = 0;
  logPageTotal = 0;
  elements.settingsList.replaceChildren();
  elements.logTabs.replaceChildren();
  elements.logContent.textContent = '';
  setConnection('connecting', 'Connecting…');
  showNotice('Select the device port. The app will wait if opening it restarts the device.', '');
  try {
    serial = new SerialProtocol((frame) => serial?.dispatch(frame), (error) => {
      clearInterval(pollTimer);
      pollTimer = 0;
      serial = null;
      deviceSettings = null;
      logInfo = null;
      logRecords = [];
      logStartOffset = 0;
      logPageTotal = 0;
      elements.settingsList.replaceChildren();
      elements.logTabs.replaceChildren();
      elements.logContent.textContent = '';
      elements.settingsStatus.textContent = 'Connect to read settings.';
      setConnection('disconnected', 'Connection lost');
      showNotice(error.message, 'error');
    });
    await serial.connect();
    setConnection('connected', 'Connected');
    showNotice('Waiting 1 second before HELLO…', '');
    const hello = await requestHelloAfterBoot(serial);
    handleHello(hello);
    showNotice('Device connected. Readings update once per second.', 'success');
    await loadSettings();
    await loadLogInfo();
    await refreshStatus();
    pollTimer = setInterval(refreshStatus, 1000);
  } catch (error) {
    if (serial) await serial.disconnect();
    serial = null;
    deviceSettings = null;
    logInfo = null;
    logRecords = [];
    logPageTotal = 0;
    elements.settingsList.replaceChildren();
    elements.logTabs.replaceChildren();
    elements.logContent.textContent = '';
    elements.settingsStatus.textContent = 'Connect to read settings.';
    setConnection('disconnected', 'Disconnected');
    showNotice(error.message || 'Could not connect to the device.', 'error');
  }
}

async function disconnectDevice() {
  clearInterval(pollTimer);
  pollTimer = 0;
  if (serial) await serial.disconnect();
  serial = null;
  polling = false;
  deviceSettings = null;
  logInfo = null;
  logRecords = [];
  logPageTotal = 0;
  elements.settingsList.replaceChildren();
  elements.logTabs.replaceChildren();
  elements.logContent.textContent = '';
  elements.settingsStatus.textContent = 'Connect to read settings.';
  setConnection('disconnected', 'Disconnected');
  showNotice('Port closed. Last readings remain visible.', '');
}

elements.connect.addEventListener('click', () => {
  if (serial) disconnectDevice();
  else connectDevice();
});

if (!('serial' in navigator)) {
  elements.connect.disabled = true;
  showNotice('Web Serial is not supported. Use a supported browser on localhost or HTTPS.', 'error');
}
