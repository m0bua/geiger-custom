const VERSION = 1;
const KIND_REQUEST = 1;
const KIND_RESPONSE = 2;
const OP_HELLO = 1;
const OP_GET_STATUS = 2;
const OP_GET_SETTINGS = 3;
const OP_SET_SETTING = 4;
const CAP_GET_SETTINGS = 1 << 1;
const CAP_SET_SETTING = 1 << 2;
const MAX_PAYLOAD = 64;

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

const elements = {
  connect: document.querySelector('#connect-button'),
  connection: document.querySelector('#connection'),
  connectionLabel: document.querySelector('#connection-label'),
  notice: document.querySelector('#notice'),
  rateChart: document.querySelector('#rate-chart'),
  localTime: document.querySelector('#local-time'),
  solarPercent: document.querySelector('#solar-percent'),
  batteryPercent: document.querySelector('#battery-percent'),
  overviewTab: document.querySelector('#overview-tab'),
  debugTab: document.querySelector('#debug-tab'),
  settingsTab: document.querySelector('#settings-tab'),
  logsTab: document.querySelector('#logs-tab'),
  firmwareTab: document.querySelector('#firmware-tab'),
  overviewPage: document.querySelector('#overview-page'),
  debugPage: document.querySelector('#debug-page'),
  settingsPage: document.querySelector('#settings-page'),
  logsPage: document.querySelector('#logs-page'),
  firmwarePage: document.querySelector('#firmware-page'),
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
  battery: document.querySelector('#battery'),
  solar: document.querySelector('#solar'),
  usbRaw: document.querySelector('#usb-raw'),
  hv: document.querySelector('#hv-state'),
  usb: document.querySelector('#usb-state'),
  device: document.querySelector('#device-info'),
  updated: document.querySelector('#last-update'),
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

function updateLocalTime() {
  elements.localTime.textContent = new Intl.DateTimeFormat('en-GB', {
    hour: '2-digit', minute: '2-digit', hour12: false,
  }).format(new Date());
}

function percentFromAdc(raw, empty, full) {
  if (raw <= empty) return 0;
  if (raw >= full) return 100;
  return Math.round(((raw - empty) * 100) / (full - empty));
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
  const digits = value === 0 ? 0 : Math.max(0, Math.min(5, 5 - integerDigits));
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
  if (rateHistory.length < 2) return;

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
  context.fillStyle = 'rgba(0,229,139,.08)';
  context.fill();
  context.beginPath();
  points.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y));
  context.strokeStyle = '#00e58b';
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
  const pages = ['overview', 'debug', 'settings', 'logs', 'firmware'];
  for (const name of pages) {
    const active = name === page;
    elements[`${name}Page`].hidden = !active;
    elements[`${name}Tab`].classList.toggle('active', active);
    elements[`${name}Tab`].setAttribute('aria-selected', String(active));
  }
  if (page === 'overview') drawRateChart();
  if (page === 'settings' && serial && deviceSettings === null) loadSettings();
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
  elements.device.textContent = `Protocol ${protocolVersion}`;
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
  if (payload.length !== 25 || payload[0] !== 0) throw new ProtocolError('Device rejected GET_STATUS or returned an unknown format.');
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const uptimeSeconds = view.getUint32(1, true);
  const count = view.getUint32(5, true);
  const cpm = view.getUint32(9, true);
  const doseNsv = view.getUint32(13, true);
  const batteryRaw = view.getUint16(17, true);
  const solarRaw = view.getUint16(19, true);
  const usbRaw = view.getUint16(21, true);
  const flags = view.getUint16(23, true);
  const uptime = formatUptime(uptimeSeconds);
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
    elements.rateLabel.textContent = 'RATE · CALCULATED';
    elements.rateDose.textContent = rate.value;
    elements.rateUnit.textContent = rate.unit;
    elements.dose.textContent = dose.value;
    elements.doseUnit.textContent = dose.unit;
    elements.rate.textContent = cpm.toLocaleString('en-US');
    elements.rateFoot.hidden = false;
    elements.doseFoot.hidden = false;
  }
  elements.count.textContent = count.toLocaleString('en-US');
  elements.uptime.textContent = `${uptime.days}d ${uptime.clock}`;
  rateHistory.push(unit === 1 ? cpm : rateUsvh);
  if (rateHistory.length > 60) rateHistory.shift();
  drawRateChart();
  elements.battery.textContent = batteryRaw.toLocaleString('en-US');
  elements.solar.textContent = solarRaw.toLocaleString('en-US');
  elements.usbRaw.textContent = usbRaw.toLocaleString('en-US');
  const solarPercent = Math.min(100, Math.round(solarRaw / 31));
  elements.solarPercent.textContent = `${solarPercent}%`;
  elements.solarPercent.classList.toggle('solar-warn', solarPercent > 49 && solarPercent <= 79);
  elements.solarPercent.classList.toggle('solar-good', solarPercent > 79);
  elements.batteryPercent.textContent = `${percentFromAdc(batteryRaw, 1300, 2000)}%`;
  setStateValue(elements.hv, (flags & 1) !== 0, 'On', 'Off');
  setStateValue(elements.usb, (flags & 2) !== 0, 'Connected', 'Not detected');
  elements.updated.textContent = `Updated ${new Date().toLocaleTimeString('en-US')}`;
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
  // Diagnostic pause: observe Web Serial open before sending the first byte.
  await delay(5000);
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
elements.firmwareTab.addEventListener('click', () => selectPage('firmware'));
window.addEventListener('resize', drawRateChart);
updateLocalTime();
setInterval(updateLocalTime, 1000);

async function connectDevice() {
  protocolCapabilities = 0;
  deviceSettings = null;
  elements.settingsList.replaceChildren();
  setConnection('connecting', 'Connecting…');
  showNotice('Select the device port. The app will wait if opening it restarts the device.', '');
  try {
    serial = new SerialProtocol((frame) => serial?.dispatch(frame), (error) => {
      clearInterval(pollTimer);
      pollTimer = 0;
      serial = null;
      deviceSettings = null;
      elements.settingsList.replaceChildren();
      elements.settingsStatus.textContent = 'Connect to read settings.';
      setConnection('disconnected', 'Connection lost');
      showNotice(error.message, 'error');
    });
    await serial.connect();
    setConnection('connected', 'Connected');
    showNotice('Waiting 5 seconds before HELLO…', '');
    const hello = await requestHelloAfterBoot(serial);
    handleHello(hello);
    showNotice('Device connected. Readings update once per second.', 'success');
    await loadSettings();
    await refreshStatus();
    pollTimer = setInterval(refreshStatus, 1000);
  } catch (error) {
    if (serial) await serial.disconnect();
    serial = null;
    deviceSettings = null;
    elements.settingsList.replaceChildren();
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
  elements.settingsList.replaceChildren();
  elements.settingsStatus.textContent = 'Connect to read settings.';
  setConnection('disconnected', 'Disconnected');
  elements.device.textContent = '—';
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
