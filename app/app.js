const VERSION = 1;
const KIND_REQUEST = 1;
const KIND_RESPONSE = 2;
const OP_HELLO = 1;
const OP_GET_STATUS = 2;
const MAX_PAYLOAD = 64;

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

const elements = {
  connect: document.querySelector('#connect-button'),
  disconnect: document.querySelector('#disconnect-button'),
  connection: document.querySelector('#connection'),
  connectionLabel: document.querySelector('#connection-label'),
  notice: document.querySelector('#notice'),
  rateChart: document.querySelector('#rate-chart'),
  localTime: document.querySelector('#local-time'),
  solarPercent: document.querySelector('#solar-percent'),
  batteryPercent: document.querySelector('#battery-percent'),
  overviewTab: document.querySelector('#overview-tab'),
  debugTab: document.querySelector('#debug-tab'),
  overviewPage: document.querySelector('#overview-page'),
  debugPage: document.querySelector('#debug-page'),
  rate: document.querySelector('#rate'),
  rateDose: document.querySelector('#rate-dose'),
  dose: document.querySelector('#dose'),
  count: document.querySelector('#count'),
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
const rateHistory = [];

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
  const debug = page === 'debug';
  elements.overviewPage.hidden = debug;
  elements.debugPage.hidden = !debug;
  elements.overviewTab.classList.toggle('active', !debug);
  elements.debugTab.classList.toggle('active', debug);
  elements.overviewTab.setAttribute('aria-selected', String(!debug));
  elements.debugTab.setAttribute('aria-selected', String(debug));
  if (!debug) drawRateChart();
}

function setConnection(state, label) {
  elements.connection.dataset.state = state;
  elements.connectionLabel.textContent = label;
  elements.connect.disabled = state === 'connecting' || state === 'connected';
  elements.disconnect.disabled = state !== 'connected';
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
  elements.device.textContent = `Protocol ${protocolVersion}`;
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
  const rateUsvh = cpm * 0.00666;

  elements.rate.textContent = cpm.toLocaleString('en-US');
  elements.rateDose.textContent = rateUsvh.toFixed(3);
  elements.dose.textContent = (doseNsv / 1000).toFixed(3);
  elements.count.textContent = count.toLocaleString('en-US');
  elements.uptime.textContent = `${uptime.days}d ${uptime.clock}`;
  rateHistory.push(rateUsvh);
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
window.addEventListener('resize', drawRateChart);
updateLocalTime();
setInterval(updateLocalTime, 1000);

elements.connect.addEventListener('click', async () => {
  setConnection('connecting', 'Connecting…');
  showNotice('Select the device port. The app will wait if opening it restarts the device.', '');
  try {
    serial = new SerialProtocol((frame) => serial?.dispatch(frame), (error) => {
      clearInterval(pollTimer);
      pollTimer = 0;
      serial = null;
      setConnection('disconnected', 'Connection lost');
      showNotice(error.message, 'error');
    });
    await serial.connect();
    setConnection('connected', 'Connected');
    showNotice('Waiting 5 seconds before HELLO…', '');
    const hello = await requestHelloAfterBoot(serial);
    handleHello(hello);
    showNotice('Device connected. Readings update once per second.', 'success');
    await refreshStatus();
    pollTimer = setInterval(refreshStatus, 1000);
  } catch (error) {
    if (serial) await serial.disconnect();
    serial = null;
    setConnection('disconnected', 'Disconnected');
    showNotice(error.message || 'Could not connect to the device.', 'error');
  }
});

elements.disconnect.addEventListener('click', async () => {
  clearInterval(pollTimer);
  pollTimer = 0;
  if (serial) await serial.disconnect();
  serial = null;
  polling = false;
  setConnection('disconnected', 'Disconnected');
  elements.device.textContent = '—';
  showNotice('Port closed. Last readings remain visible.', '');
});

if (!('serial' in navigator)) {
  elements.connect.disabled = true;
  showNotice('Web Serial is not supported. Use a supported browser on localhost or HTTPS.', 'error');
}
