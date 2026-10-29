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
const OP_SPI_PROGRAM_PAGE = 10;
const OP_SPI_ERASE_SECTOR = 11;
const CAP_GET_STATUS = 1 << 0;
const OP_SET_RTC = 8;
const OP_DEVICE_ACTION = 9;
const CAP_GET_SETTINGS = 1 << 1;
const CAP_SET_SETTING = 1 << 2;
const CAP_GET_SPI_DUMP = 1 << 5;
const CAP_SET_RTC = 1 << 6;
const CAP_DEVICE_ACTION = 1 << 7;
const MAX_PAYLOAD = 137;
const LOG_BATCH_SIZE = 10;
const LOG_INITIAL_TARGETS = { minutes: 60, hours: 24, days: 30, alerts: 5 };
const STM32_FLASH_BASE = 0x08000000;
const STM32_FLASH_SIZE = 64 * 1024;
const STM32_FLASH_PAGE_SIZE = 128;
const STM32_EEPROM_BASE = 0x08080000;
const STM32_EEPROM_SIZE = 2 * 1024;
const SPI_NOR_SIZE = 1024 * 1024;
const SPI_SECTOR_SIZE = 4096;
const MEMORY_PRESETS = {
  eeprom: {
    full: [0, 2047], settings: [0, 347], minutes: [348, 905],
    hours: [906, 1130], days: [1131, 1580], alerts: [1581, 1910],
    'all-logs': [348, 1910]
  },
  spi: {
    full: [0, 1048575], minutes: [0, 368639], hours: [368640, 491519],
    days: [491520, 696319], alerts: [696320, 1048575],
    'all-logs': [0, 1048575]
  }
};
const SPI_DUMP_CHUNK_SIZE = 58;
const SPI_RECOVERY_DB = 'dosimeter-spi-recovery';
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
  'a18769eb3937e0bdd7ca1c3e263f8e2ad3ece6b60795c81ac13c6a6ecd71da20',
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

  async goToAddress(address) {
    await this.command(0x21, 0xde, 'Go');
    const bytes = [
      (address >>> 24) & 0xff,
      (address >>> 16) & 0xff,
      (address >>> 8) & 0xff,
      address & 0xff,
    ];
    bytes.push(bytes.reduce((checksum, byte) => checksum ^ byte, 0));
    await this.write(bytes);
    await this.expectAck('Go address');
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
  connectUsb: document.querySelector('#connect-usb-button'),
  bridgePort: document.querySelector('#windows-bridge-port'),
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
  logRangeMode: document.querySelector('#log-range-mode'),
  logRangeExtra: document.querySelector('#log-range-extra'),
  logFillMetric: document.querySelector('#log-fill-metric'),
  exportLogsCsv: document.querySelector('#export-logs-csv'),
  logsDeviceTime: document.querySelector('#logs-device-time'),
  loadLogSelection: document.querySelector('#load-log-selection'),
  logsStatus: document.querySelector('#logs-status'),
  loadMoreLogs: document.querySelector('#load-more-logs'),
  logDiagnostics: document.querySelector('#log-diagnostics'),
  copyLogDiagnostics: document.querySelector('#copy-log-diagnostics'),
  clearLogDiagnostics: document.querySelector('#clear-log-diagnostics'),
  firmwarePage: document.querySelector('#firmware-page'),
  embeddedQuickFlash: document.querySelector('#embedded-quick-flash'),
  firmwareTools: document.querySelector('#firmware-tools'),
  quickBackupFlash: document.querySelector('#quick-backup-flash'),
  quickRestoreFlash: document.querySelector('#quick-restore-flash'),
  quickFlashFirmware: document.querySelector('#quick-flash-firmware'),
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
  firmwareRestoreFlash: document.querySelector('#firmware-restore-flash'),
  firmwareLog: document.querySelector('#firmware-log'),
  dumpFlash: document.querySelector('#dump-flash'),
  dumpEeprom: document.querySelector('#dump-eeprom'),
  dumpSpi: document.querySelector('#dump-spi'),
  eraseEeprom: document.querySelector('#erase-eeprom'),
  eraseSpi: document.querySelector('#erase-spi'),
  eepromRangePreset: document.querySelector('#eeprom-range-preset'),
  eepromAddressFrom: document.querySelector('#eeprom-address-from'),
  eepromAddressTo: document.querySelector('#eeprom-address-to'),
  spiRangePreset: document.querySelector('#spi-range-preset'),
  spiAddressFrom: document.querySelector('#spi-address-from'),
  spiAddressTo: document.querySelector('#spi-address-to'),
  dumpStatus: document.querySelector('#dump-status'),
  spiNorSection: document.querySelector('#spi-nor-section'),
  spiRecoveryControls: document.querySelector('#spi-recovery-controls'),
  downloadSpiBackup: document.querySelector('#download-spi-backup'),
  eepromImageFile: document.querySelector('#eeprom-image-file'),
  clearEepromImage: document.querySelector('#clear-eeprom-image'),
  writeEepromImage: document.querySelector('#write-eeprom-image'),
  eepromImageStatus: document.querySelector('#eeprom-image-status'),
  eepromWriteStatus: document.querySelector('#eeprom-write-status'),
  spiImageFile: document.querySelector('#spi-image-file'),
  clearSpiImage: document.querySelector('#clear-spi-image'),
  writeSpiImage: document.querySelector('#write-spi-image'),
  spiImageStatus: document.querySelector('#spi-image-status'),
  spiWriteStatus: document.querySelector('#spi-write-status'),
  manualPage: document.querySelector('#manual-page'),
  aboutPage: document.querySelector('#about-page'),
  settingsStatus: document.querySelector('#settings-status'),
  settingsList: document.querySelector('#settings-list'),
  spiConfirmDialog: document.querySelector('#spi-confirm-dialog'),
  spiConfirmCancel: document.querySelector('#spi-confirm-cancel'),
  spiConfirmAccept: document.querySelector('#spi-confirm-accept'),
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
  debugRtcClock: document.querySelector('#debug-rtc-clock'),
  debugSeconds: document.querySelector('#debug-seconds'),
  debugDate: document.querySelector('#debug-date'),
  debugTime: document.querySelector('#debug-time'),
  debugModeBadge: document.querySelector('#debug-mode-badge'),
  debugSpi: document.querySelector('#debug-spi'),
  debugJedec: document.querySelector('#debug-jedec'),
};

class ProtocolError extends Error {}

function confirmSpiEnable() {
  const dialog = elements.spiConfirmDialog;
  if (!dialog || typeof dialog.showModal !== 'function') {
    return Promise.resolve(window.confirm(
      'Enabling SPI will erase all assets stored on the SPI chip. Returning to stock firmware will not be possible. Continue?'));
  }
  return new Promise((resolve) => {
    const onClose = () => {
      dialog.removeEventListener('close', onClose);
      resolve(dialog.returnValue === 'confirm');
    };
    dialog.addEventListener('close', onClose);
    dialog.showModal();
  });
}

elements.spiConfirmCancel?.addEventListener('click', () =>
  elements.spiConfirmDialog.close('cancel'));
elements.spiConfirmAccept?.addEventListener('click', () =>
  elements.spiConfirmDialog.close('confirm'));

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

class Cp210xUsbPort {
  constructor() {
    this.device = null;
    this.interfaceNumber = -1;
    this.inEndpoint = null;
    this.outEndpoint = null;
    this.claimed = false;
    this.closing = false;
    this.readable = null;
    this.writable = null;
  }

  getInfo() {
    return { usbVendorId: this.device?.vendorId, usbProductId: this.device?.productId };
  }

  async controlOut(request, value = 0, data = new Uint8Array()) {
    const result = await this.device.controlTransferOut({
      requestType: 'vendor', recipient: 'interface', request, value,
      index: this.interfaceNumber,
    }, data);
    if (result.status !== 'ok') throw new Error(`CP210x control request 0x${request.toString(16)} failed (${result.status}).`);
  }

  async open(options = {}) {
    if (!navigator.usb) throw new Error('WebUSB is unavailable in this browser.');
    let stage = 'checking permitted devices';
    try {
      if (!this.device) {
        const permitted = await navigator.usb.getDevices();
        this.device = permitted.find((device) =>
          device.vendorId === 0x10c4 && device.productId === 0xea60) || null;
      }
      if (!this.device) {
        stage = 'requesting CP210x USB permission';
        this.device = await navigator.usb.requestDevice({
          filters: [{ vendorId: 0x10c4, productId: 0xea60 }],
        });
      }
      if (!this.device.opened) {
        stage = 'opening the CP210x USB device';
        await this.device.open();
      }
      if (!this.device.configuration) {
        stage = 'selecting the CP210x USB configuration';
        const config = this.device.configurations[0];
        if (!config) throw new Error('The CP210x adapter has no USB configuration.');
        await this.device.selectConfiguration(config.configurationValue);
      }
      let selected = null;
      for (const iface of this.device.configuration.interfaces) {
        for (const alternate of iface.alternates) {
          const input = alternate.endpoints.find((ep) => ep.type === 'bulk' && ep.direction === 'in');
          const output = alternate.endpoints.find((ep) => ep.type === 'bulk' && ep.direction === 'out');
          if (input && output) { selected = { iface, alternate, input, output }; break; }
        }
        if (selected) break;
      }
      if (!selected) throw new Error('The CP210x USB interface has no bulk UART endpoints.');
      this.interfaceNumber = selected.iface.interfaceNumber;
      stage = `claiming CP210x USB interface ${this.interfaceNumber}`;
      await this.device.claimInterface(this.interfaceNumber);
      this.claimed = true;
      if (selected.iface.alternate.alternateSetting !== selected.alternate.alternateSetting) {
        stage = 'selecting the CP210x USB alternate interface';
        await this.device.selectAlternateInterface(this.interfaceNumber,
          selected.alternate.alternateSetting);
      }
      this.inEndpoint = selected.input.endpointNumber;
      this.outEndpoint = selected.output.endpointNumber;

      stage = 'configuring CP210x UART';
      await this.controlOut(0x00, 0x0001); // IFC_ENABLE / UART_ENABLE
      const parityBits = options.parity === 'even' ? 0x0020 :
        options.parity === 'odd' ? 0x0010 : 0x0000;
      const stopBits = options.stopBits === 2 ? 0x0002 : 0x0000;
      await this.controlOut(0x03, 0x0800 | parityBits | stopBits); // SET_LINE_CTL
      const baud = new Uint8Array(4);
      new DataView(baud.buffer).setUint32(0, options.baudRate || 115200, true);
      await this.controlOut(0x1e, 0, baud); // SET_BAUDRATE
      this.closing = false;
      this.readable = new ReadableStream({
        pull: (controller) => this.pullInput(controller),
        cancel: () => { this.closing = true; },
      });
      this.writable = new WritableStream({
        write: (chunk) => this.writeOutput(chunk),
        close: () => {},
        abort: () => { this.closing = true; },
      });
    } catch (error) {
      if (this.device?.opened) {
        try { await this.device.close(); } catch { /* retain the initial USB error */ }
      }
      if (error?.name === 'NotFoundError') {
        throw new Error('No CP210x adapter was selected. Check the OTG connection and choose USB Connect again.');
      }
      const detail = error?.message ? `: ${error.message}` : '';
      throw new Error(`WebUSB failed while ${stage} (${error?.name || 'Error'})${detail}.`);
    }
  }

  async pullInput(controller) {
    if (this.closing) { controller.close(); return; }
    try {
      const result = await this.device.transferIn(this.inEndpoint, 64);
      if (this.closing) { controller.close(); return; }
      if (result.status !== 'ok') throw new Error(`CP210x USB read failed (${result.status}).`);
      if (result.data?.byteLength) {
        controller.enqueue(new Uint8Array(result.data.buffer,
          result.data.byteOffset, result.data.byteLength).slice());
      }
    } catch (error) {
      if (this.closing) controller.close();
      else controller.error(error);
    }
  }

  async writeOutput(chunk) {
    if (this.closing || !this.device?.opened) throw new Error('USB connection is closed.');
    const bytes = chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk);
    for (let offset = 0; offset < bytes.length; offset += 256) {
      const result = await this.device.transferOut(this.outEndpoint,
        bytes.subarray(offset, Math.min(offset + 256, bytes.length)));
      if (result.status !== 'ok') throw new Error(`CP210x USB write failed (${result.status}).`);
    }
  }

  async setSignals({ dataTerminalReady = false, requestToSend = false } = {}) {
    if (!this.device?.opened) throw new Error('CP210x USB device is not open.');
    const value = 0x0100 | 0x0200 |
      (dataTerminalReady ? 0x0001 : 0) |
      (requestToSend ? 0x0002 : 0);
    await this.controlOut(0x07, value); // SET_MHS, explicit DTR/RTS update
  }

  async abort() {
    this.closing = true;
    if (this.device?.opened) {
      try { await this.device.close(); } catch { /* close() retries cleanup below */ }
    }
  }

  async close() {
    this.closing = true;
    if (!this.device?.opened) { this.claimed = false; return; }
    if (this.claimed) {
      try { await this.device.releaseInterface(this.interfaceNumber); }
      finally { this.claimed = false; }
    }
    await this.device.close();
  }
}

class NativeBridgePort {
  constructor(portName) {
    this.portName = portName;
    this.socket = null;
    this.readable = null;
    this.writable = null;
    this.readController = null;
    this.readQueue = [];
    this.pending = new Map();
    this.nextId = 1;
    this.signals = { dataTerminalReady: false, requestToSend: false };
  }

  getInfo() { return { usbVendorId: 0x10c4, usbProductId: 0xea60 }; }

  async command(op, values = {}) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Local serial bridge timed out during ${op}.`));
      }, 5000);
      this.pending.set(id, { resolve, reject, timeout });
      this.socket.send(JSON.stringify({ op, id, ...values }));
    });
  }

  receiveMessage(event) {
    if (typeof event.data !== 'string') {
      const bytes = new Uint8Array(event.data);
      if (this.readController) this.readController.enqueue(bytes);
      else this.readQueue.push(bytes);
      return;
    }
    let message;
    try { message = JSON.parse(event.data); } catch { return; }
    if (message.type === 'error' && !message.id) {
      this.readController?.error(new Error(message.message || 'Local COM port failed.'));
      return;
    }
    const pending = this.pending.get(message.id);
    if (!pending) return;
    clearTimeout(pending.timeout);
    this.pending.delete(message.id);
    if (message.type === 'error') pending.reject(new Error(message.message || 'Local bridge error.'));
    else pending.resolve(message);
  }

  async open(options = {}) {
    const socket = new WebSocket(`ws://${location.host}/serial`);
    socket.binaryType = 'arraybuffer';
    this.socket = socket;
    await new Promise((resolve, reject) => {
      socket.addEventListener('open', resolve, { once: true });
      socket.addEventListener('error', () => reject(new Error('Could not connect to the local serial bridge.')), { once: true });
    });
    socket.addEventListener('message', (event) => this.receiveMessage(event));
    socket.addEventListener('close', () => {
      for (const pending of this.pending.values()) {
        clearTimeout(pending.timeout);
        pending.reject(new Error('Local serial bridge disconnected.'));
      }
      this.pending.clear();
      try { this.readController?.close(); } catch { /* stream already closed */ }
    });
    await this.command('open', {
      port: this.portName, baudRate: options.baudRate || 115200,
      dataBits: options.dataBits || 8, parity: options.parity || 'none',
      stopBits: options.stopBits || 1, dtr: false, rts: false,
    });
    this.readable = new ReadableStream({
      start: (controller) => {
        this.readController = controller;
        while (this.readQueue.length) controller.enqueue(this.readQueue.shift());
      },
      cancel: () => { this.readController = null; },
    });
    this.writable = new WritableStream({
      write: async (chunk) => {
        const id = this.nextId++;
        const bytes = chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk);
        const frame = new Uint8Array(4 + bytes.length);
        new DataView(frame.buffer).setUint32(0, id, true);
        frame.set(bytes, 4);
        await new Promise((resolve, reject) => {
          const timeout = setTimeout(() => {
            this.pending.delete(id);
            reject(new Error('Local serial write timed out.'));
          }, 5000);
          this.pending.set(id, { resolve, reject, timeout });
          this.socket.send(frame);
        });
      },
    });
  }

  async setSignals({ dataTerminalReady, requestToSend } = {}) {
    if (dataTerminalReady !== undefined) this.signals.dataTerminalReady = dataTerminalReady;
    if (requestToSend !== undefined) this.signals.requestToSend = requestToSend;
    await this.command('signals', {
      dtr: this.signals.dataTerminalReady, rts: this.signals.requestToSend,
    });
  }

  async abort() { /* Keep the bridge alive until queued serial writes drain. */ }

  async close() {
    if (!this.socket) return;
    try { await this.command('close'); } finally {
      this.socket.close();
      this.socket = null;
      this.readController = null;
      this.readable = null;
      this.writable = null;
    }
  }
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
    this.activeWrites = new Set();
    this.frameBytes = [];
    this.discardFrame = false;
  }

  async connect(portOverride = null, transport = 'serial') {
    if (transport === 'native') {
      this.port = portOverride || new NativeBridgePort(selectedBridgePort);
      await this.port.open({ baudRate: 115200, dataBits: 8, parity: 'none', stopBits: 1 });
      selectedNativePort = this.port;
      selectedSerialPort = null;
      selectedUsbPort = null;
      activeTransport = 'native';
    } else if (transport === 'webusb') {
      this.port = portOverride || new Cp210xUsbPort();
      await this.port.open();
      selectedUsbPort = this.port;
      selectedSerialPort = null;
      activeTransport = 'webusb';
    } else {
      if (!('serial' in navigator)) {
        throw new Error('Web Serial is unavailable here. On Android, use Connect USB with a CP210x adapter.');
      }
      this.port = portOverride || await navigator.serial.requestPort();
      selectedSerialPort = this.port;
      this.portInfo = this.port.getInfo?.() || {};
      await this.port.open({ baudRate: 115200, dataBits: 8, parity: 'none', stopBits: 1, flowControl: 'none', bufferSize: 256 });
      activeTransport = 'serial';
    }
    this.portInfo = this.portInfo || this.port.getInfo?.() || {};
    portMayBeOpen = true;
    // Preserve the first-release Web Serial behavior. The native Windows
    // bridge applies its own initial DTR/RTS profile when opening the COM port.
    if (transport === 'serial')
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

  async request(opcode, payload = new Uint8Array(), timeoutMs = null) {
    if (!this.writer || this.closing) throw new Error('Serial port is not open.');
    const requestTimeout = timeoutMs ??
      (this.port instanceof Cp210xUsbPort ? 3500 : 1800);
    this.requestId = (this.requestId + 1) & 0xffff;
    if (this.requestId === 0) this.requestId = 1;
    const id = this.requestId;
    const promise = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error('Device did not respond. Check the USB connection and try again.'));
      }, requestTimeout);
      this.pending.set(id, { opcode, resolve, reject, timeout });
    });
    try {
      const writeTask = this.writer.write(encodeFrame(KIND_REQUEST, id, opcode, payload));
      this.activeWrites.add(writeTask);
      try { await writeTask; }
      finally { this.activeWrites.delete(writeTask); }
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
    if (this.port?.abort) await this.port.abort();
    await Promise.allSettled(this.activeWrites);
    if (this.reader) {
      try { await this.reader.cancel(); } catch { /* port may already be gone */ }
    }
    if (this.readTask) await this.readTask.catch(() => {});
    if (this.writer) {
      try { await this.writer.close(); } catch { /* device may have been removed */ }
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
let installPromptEvent = null;
let rateHoverIndex = null;
let selectedSerialPort = null;
let selectedUsbPort = null;
let selectedNativePort = null;
let selectedBridgePort = '';
let nativeBridgeAvailable = false;
let bridgePortsKey = null;
let activeTransport = 'serial';
const webUsbAvailable = !!navigator.usb;
const androidUsbAvailable = webUsbAvailable && /Android/i.test(navigator.userAgent);
let bootloaderPortOpen = false;
let portMayBeOpen = false;
let selectedFirmwareImage = null;
let spiRecoveryBackup = null;
let latestFlashBackup = null;
let spiMiniImageCache = null;
let selectedEepromImage = null;
let selectedSpiImage = null;
let firmwareFlashBusy = false;
let firmwareStopRequested = false;
let dumpBusy = false;
let pollTimer = 0;
let polling = false;
let protocolCapabilities = 0;
let protocolMaxPayload = 64;
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
let logInfoPollingFailed = false;
const logViewCache = new Map();
const logTypeActiveView = new Map();
const logRangeInputCache = new Map();
let logCursor = { time: 0xffffffff, epoch: 0xffff, skip: 0 };
let logHasMore = true;
let logSelectedDate = "";
let logRangeActive = false;
let logRangeAll = false;
let logCountLimit = 0;
let logRangeStart = 0;
let logRangeEnd = 0;
let logCsvBusy = false;
const logRangeDefaults = new Set();
let logFillMetric = 'dose';
const logDiagnostics = [];
let demoLogRenderer = null;
let clockSyncBusy = false;
const rateHistory = [];
const demoSettings = Array.from({ length: 29 }, () => 0);
const demoHiddenSettings = new Set([
  8, 10, 11, 13, 14, 15, 16, 17, 19, 20, 21, 24, 25, 26, 27,
]);
Object.assign(demoSettings, {
  0: 2, 5: 0, 6: 0, 7: 2, 9: 0, 10: 1, 11: 1, 12: 0, 22: 1,
  15: 0, 16: 4, 21: 3, 23: 1,
});

const remoteSettings = [
  { id: 23, label: 'DOSE PERIOD', options: ['UPTIME', 'DAY', 'TOTAL'] },
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
  { id: 12, label: 'RATE ROW 2', options: [] },
  { id: 22, label: 'DOSE ROW 2', options: [] },
  { id: 5, label: 'LED BRIGHT', options: ['AUTO', '1%', '3%', '5%', '10%', '25%', '50%', '80%', '100%'] },
  { id: 6, label: 'SCREEN BRIGHT', options: ['AUTO', '1%', '3%', '5%', '10%', '25%', '50%', '80%', '100%'] },
  { id: 7, label: 'SCREEN TIMER', options: ['1 s', '5 s', '10 s', '30 s', '60 s', '90 s'] },
  { id: 20, label: 'TONE', options: ['STOCK', 'CLASSIC'] },
  { id: 21, label: 'LONG PRESS', options: ['300 ms', '500 ms', '700 ms', '900 ms', '1100 ms', '1300 ms'] },
  { id: 9, label: 'UNIT', options: ['Sv', 'CNT', 'R'] },
  { id: 10, label: 'RATE PREFIX', options: ['m', 'µ', 'n', 'AUTO'] },
  { id: 11, label: 'DOSE PREFIX', options: ['m', 'µ', 'n', 'AUTO'] },
  { id: 8, label: 'LEAD ZERO', options: ['SHOW', 'HIDE'] },
  { id: 15, label: 'HV', options: ['ON', 'OFF'] },
  { id: 13, label: 'LOG MEMORY', options: ['EEPROM', 'SPI', 'OFF'] },
  { id: 14, label: 'RESET', options: ['SETTINGS', 'LOGS', 'OLD LOGS', 'EEPROM', 'SPI', 'ALL'] },
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

function logDiagnostic(message, error = null) {
  const stamp = new Date().toISOString().slice(11, 23);
  const detail = error ? ` ${error.name || 'Error'}: ${error.message || error}` : '';
  const line = `[${stamp}] ${message}${detail}`;
  logDiagnostics.push(line);
  if (logDiagnostics.length > 400) logDiagnostics.shift();
  if (elements.logDiagnostics) elements.logDiagnostics.textContent = logDiagnostics.join('\n');
  if (error) console.error(line, error); else console.debug(line);
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
  else if (page === 'logs' && serial && logInfo && !logRecords.length) {
    if (restoreLogView()) {
      renderLogSummary();
      if (logHasMore) loadLogPage(false);
    }
    else loadLogSelection();
  } else if (page === 'logs' && !serial) {
    updateLogRangeControls();
  }
}

function renderDebugLogInfo() {
  if (!logInfo) return;
  const rtcStatus = logInfo.rtcStatus ||
    (logInfo.calendarValid ? 'SET' : 'NOT SET');
  elements.debugRtcClock.textContent = `${rtcStatus} ${logInfo.clockSource || '—'}`;
  elements.debugSeconds.textContent = formatGroupedNumber(logInfo.seconds);
  const spiError = !logInfo.spiSizeMb;
  elements.debugSpi.textContent = spiError ? 'ERR' : `${logInfo.spiSizeMb} MB`;
  elements.debugSpi.classList.toggle('state-error', spiError);
  elements.debugJedec.textContent = logInfo.jedec ? logInfo.jedec : '—';
}

function applyLogInfo(payload) {
  if ((payload.length !== 30 && payload.length !== 35) || payload[0] !== 0 || payload[26] !== 2) {
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
    spiFaultOperation: payload.length >= 35 ? payload[30] : 0,
    spiFaultStage: payload.length >= 35 ? payload[31] : 0,
    spiFaultAddress: payload.length >= 35 ? payload[32] | (payload[33] << 8) | (payload[34] << 16) : 0,
  };
  if (!logRangeActive && logInfo.calendarValid) updateLogRangeControls();
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
    if (serial === activeSerial) {
      if (logInfoPollingFailed) logDiagnostic('GET_LOG_INFO polling resumed.');
      logInfoPollingFailed = false;
      const before = logInfo;
      applyLogInfo(payload);
      if (before && (before.spiFaultOperation !== logInfo.spiFaultOperation ||
          before.spiFaultStage !== logInfo.spiFaultStage || before.spiFaultAddress !== logInfo.spiFaultAddress) &&
          logInfo.spiFaultOperation) {
        logDiagnostic(`SPI NOR fault captured: ${spiFaultDescription(logInfo)}`);
      }
      if (before && !before.error && logInfo.error) {
        logDiagnostic(`STORE ERR became active; memory=${logMemoryName(logInfo.memory)}, source=${logMemoryName(logInfo.source)}, SPI-ready=${logInfo.spiReady}, JEDEC=${logInfo.jedec || '—'}`);
      } else if (before?.error && !logInfo.error) {
        logDiagnostic('STORE ERR cleared by firmware.');
      } else if (before && ['minutes', 'hours', 'days', 'alerts'].some((key) => before[key] !== logInfo[key])) {
        logDiagnostic(`Stored counts changed: MIN ${before.minutes}->${logInfo.minutes}, HOUR ${before.hours}->${logInfo.hours}, DAY ${before.days}->${logInfo.days}, ALERT ${before.alerts}->${logInfo.alerts}`);
      }
    }
  } catch (error) {
    if (!logInfoPollingFailed) logDiagnostic('GET_LOG_INFO polling failed; will retry.', error);
    logInfoPollingFailed = true;
  }
  finally { logCountsRefreshBusy = false; }
}

const logTypes = [
  { id: 'minutes', protocolId: 0, label: 'MINUTES', countKey: 'minutes', period: 60 },
  { id: 'hours', protocolId: 1, label: 'HOURS', countKey: 'hours', period: 3600 },
  { id: 'days', protocolId: 2, label: 'DAYS', countKey: 'days', period: 86400 },
  { id: 'alerts', protocolId: 3, label: 'ALERTS', countKey: 'alerts', period: 60 },
];

function logMemoryName(value) {
  return ['EEPROM', 'SPI', 'OFF'][value] || `UNKNOWN(${value})`;
}

function spiFaultDescription(info) {
  if (!info?.spiFaultOperation) return 'none recorded';
  const operations = ['', 'INIT', 'READ', 'PROGRAM', 'ERASE'];
  const stages = ['', 'TXE timeout', 'RXNE timeout', 'BSY timeout',
    'WIP timeout', 'WEL not set', 'command transfer failed',
    'verify read failed', 'verify mismatch', 'JEDEC mismatch', 'invalid request/state'];
  return `${operations[info.spiFaultOperation] || `OP${info.spiFaultOperation}`} / ${stages[info.spiFaultStage] || `stage ${info.spiFaultStage}`} @ 0x${info.spiFaultAddress.toString(16).toUpperCase().padStart(6, '0')}`;
}

function logViewKey() {
  const view = logRangeActive ? (logRangeAll ? 'all' : `${logRangeStart}:${logRangeEnd}`) :
    logCountLimit ? `last:${logCountLimit}` : 'latest';
  return `${selectedLogType}:${view}`;
}
function saveLogView() {
  const key = logViewKey();
  logViewCache.set(key, {records: logRecords.slice(), cursor: {...logCursor}, more: logHasMore,
    rangeActive: logRangeActive, rangeAll: logRangeAll, countLimit: logCountLimit,
    rangeStart: logRangeStart, rangeEnd: logRangeEnd, selectedDate: logSelectedDate,
    mode: elements.logRangeMode.value});
  logTypeActiveView.set(selectedLogType, key);
}
function restoreLogView() {
  const saved = logViewCache.get(logTypeActiveView.get(selectedLogType) || logViewKey());
  if (!saved) return false;
  logRecords = saved.records.slice(); logCursor = {...saved.cursor}; logHasMore = saved.more;
  logRangeActive = saved.rangeActive; logRangeAll = saved.rangeAll;
  logCountLimit = saved.countLimit; logRangeStart = saved.rangeStart;
  logRangeEnd = saved.rangeEnd; logSelectedDate = saved.selectedDate;
  if (saved.mode) elements.logRangeMode.value = saved.mode;
  updateLogRangeControls();
  return true;
}
function clearLogView() {
  logRecords = []; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0}; logHasMore = true;
  logStartOffset = 0; logPageTotal = 0;
}
function isLatestLogView() {
  return elements.logRangeMode.value === 'latest';
}
function hasCurrentLogDate() { return isLatestLogView() || (!logRangeActive && !logCountLimit); }
function logDateLimit() {
  return logRangeActive && !logRangeAll && !isLatestLogView() ? logRangeEnd : 0;
}
function logRecordInRange(record) {
  return !logRangeActive || logRangeAll || isLatestLogView() || (record.calendar &&
    record.timestamp >= logRangeStart && record.timestamp <= logRangeEnd);
}

function calendarSecondsFromInputs(date, time = '00:00') {
  const [year, month, day] = date.split('-').map(Number);
  const [hour, minute] = time.split(':').map(Number);
  return Math.floor(Date.UTC(year, month - 1, day, hour, minute) / 1000) -
    Date.UTC(2000, 0, 1) / 1000;
}

function updateLogRangeControls() {
  const type = logTypes.find((item) => item.id === selectedLogType);
  if (!type) return;
  if (elements.logRangeExtra.dataset.type !== type.id) {
    const oldType = elements.logRangeExtra.dataset.type;
    if (oldType) {
      const saved = {};
      elements.logRangeExtra.querySelectorAll('input,select').forEach((control) => {
        saved[control.id] = control.value;
      });
      logRangeInputCache.set(oldType, saved);
    }
    elements.logRangeExtra.replaceChildren();
    if (type.id === 'minutes') {
      for (const [id, labelText] of [['log-newer-minute', 'Newer'],
        ['log-older-minute', 'Older']]) {
        const label = document.createElement('label');
        label.htmlFor = id;
        label.textContent = labelText;
        const input = document.createElement('input');
        input.id = id;
        input.type = 'datetime-local';
        input.step = '60';
        elements.logRangeExtra.append(label, input);
      }
    } else {
      for (const side of ['newer', 'older']) {
        const label = document.createElement('label');
        const date = document.createElement('input');
        date.id = `log-${side}-date`;
        date.type = 'date';
        label.htmlFor = date.id;
        label.textContent = side === 'newer' ? 'Newer' : 'Older';
        label.append(date);
        elements.logRangeExtra.append(label);
        if (type.id === 'hours') {
          const hour = document.createElement('select');
          hour.id = `log-${side}-hour`;
          for (let value = 0; value < 24; ++value) {
            const option = document.createElement('option');
            option.value = String(value).padStart(2, '0');
            option.textContent = option.value;
            hour.append(option);
          }
          elements.logRangeExtra.append(hour);
        }
      }
    }
    elements.logRangeExtra.dataset.type = type.id;
    const saved = logRangeInputCache.get(type.id) || {};
    elements.logRangeExtra.querySelectorAll('input,select').forEach((control) => {
      if (saved[control.id]) control.value = saved[control.id];
    });
    if (elements.logRangeMode.value === 'custom') initializeCustomRangeInputs(type);
  }
  if (logInfo?.calendarValid && !logRangeDefaults.has(type.id)) {
    if (elements.logRangeMode.value === 'latest') setDefaultLogRange(type);
    logRangeDefaults.add(type.id);
  }
  const custom = elements.logRangeMode.value === 'custom';
  elements.logRangeExtra.hidden = !custom;
  if (logRangeActive && !logRangeAll && custom) populateRangeInputs(type);
  const enabled = custom;
  elements.logRangeMode.disabled = logCsvBusy || logPageLoading;
  elements.logRangeExtra.querySelectorAll('input,select').forEach((control) => {
    control.disabled = !enabled || logCsvBusy || logPageLoading;
  });
  elements.loadLogSelection.disabled = !serial || !logInfo || logCsvBusy || logPageLoading ||
    (custom && !readCustomLogRange(type));
  elements.exportLogsCsv.disabled = !serial || !logRecords.length || logCsvBusy || logPageLoading;
}

function initializeCustomRangeInputs(type) {
  const now = new Date();
  const older = new Date(now);
  if (type.id === 'minutes') older.setTime(now.getTime() - 59 * 60 * 1000);
  else if (type.id === 'hours') older.setTime(now.getTime() - 23 * 3600 * 1000);
  else older.setUTCMonth(older.getUTCMonth() - 1);
  const valueFor = (date) => {
    const base = formatDeviceDate(date);
    return type.id === 'minutes' ?
      `${base}T${String(date.getUTCHours()).padStart(2, '0')}:${String(date.getUTCMinutes()).padStart(2, '0')}` : base;
  };
  for (const [side, date] of [['newer', now], ['older', older]]) {
    const input = document.querySelector(type.id === 'minutes' ?
      `#log-${side}-minute` : `#log-${side}-date`);
    if (input && !input.value) input.value = valueFor(date);
    if (type.id === 'hours') {
      const hour = document.querySelector(`#log-${side}-hour`);
      if (hour && !hour.value) hour.value = String(date.getUTCHours()).padStart(2, '0');
    }
  }
}

function setDefaultLogRange(type, force = false) {
  if (!logInfo?.calendarValid || (!force && logRangeDefaults.has(type.id))) return;
  const now = new Date(Date.UTC(2000, 0, 1) + logInfo.calendarSeconds * 1000);
  let older = new Date(now.getTime());
  if (type.id === 'minutes') {
    now.setUTCSeconds(0, 0);
    older = new Date(now.getTime() - 59 * 60 * 1000);
  } else if (type.id === 'hours') {
    now.setUTCMinutes(0, 0, 0);
    older = new Date(now.getTime() - 23 * 3600 * 1000);
  } else {
    const month = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth() - 1, 1));
    const lastDay = new Date(Date.UTC(month.getUTCFullYear(), month.getUTCMonth() + 1, 0)).getUTCDate();
    older = new Date(Date.UTC(month.getUTCFullYear(), month.getUTCMonth(),
      Math.min(now.getUTCDate(), lastDay)));
  }
  if (type.id === 'minutes') {
    document.querySelector('#log-newer-minute').value = `${formatDeviceDate(now)}T${String(now.getUTCHours()).padStart(2, '0')}:${String(now.getUTCMinutes()).padStart(2, '0')}`;
    document.querySelector('#log-older-minute').value = `${formatDeviceDate(older)}T${String(older.getUTCHours()).padStart(2, '0')}:${String(older.getUTCMinutes()).padStart(2, '0')}`;
  } else {
    for (const [side, date] of [['newer', now], ['older', older]]) {
      document.querySelector(`#log-${side}-date`).value = formatDeviceDate(date);
      if (type.id === 'hours') {
        document.querySelector(`#log-${side}-hour`).value = String(date.getUTCHours()).padStart(2, '0');
      }
    }
  }
}

function populateRangeInputs(type) {
  const older = new Date(Date.UTC(2000, 0, 1) + logRangeStart * 1000);
  const newer = new Date(Date.UTC(2000, 0, 1) + logRangeEnd * 1000);
  if (type.id === 'minutes') {
    document.querySelector('#log-newer-minute').value = `${formatDeviceDate(newer)}T${String(newer.getUTCHours()).padStart(2, '0')}:${String(newer.getUTCMinutes()).padStart(2, '0')}`;
    document.querySelector('#log-older-minute').value = `${formatDeviceDate(older)}T${String(older.getUTCHours()).padStart(2, '0')}:${String(older.getUTCMinutes()).padStart(2, '0')}`;
  } else {
    for (const [side, date] of [['newer', newer], ['older', older]]) {
      document.querySelector(`#log-${side}-date`).value = formatDeviceDate(date);
      if (type.id === 'hours') {
        document.querySelector(`#log-${side}-hour`).value = String(date.getUTCHours()).padStart(2, '0');
      }
    }
  }
}

function readCustomLogRange(type) {
  if (type.id === 'minutes') {
    const newer = document.querySelector('#log-newer-minute').value;
    const older = document.querySelector('#log-older-minute').value;
    if (!newer || !older) return null;
    const [newerDate, newerTime] = newer.split('T');
    const [olderDate, olderTime] = older.split('T');
    return {
      start: calendarSecondsFromInputs(olderDate, olderTime),
      end: calendarSecondsFromInputs(newerDate, newerTime) + 59,
    };
  }
  const newerDate = document.querySelector('#log-newer-date').value;
  const olderDate = document.querySelector('#log-older-date').value;
  if (!newerDate || !olderDate) return null;
  const newerHour = type.id === 'hours' ? document.querySelector('#log-newer-hour').value : '23';
  const olderHour = type.id === 'hours' ? document.querySelector('#log-older-hour').value : '00';
  return {
    start: calendarSecondsFromInputs(olderDate, `${olderHour}:00`),
    end: calendarSecondsFromInputs(newerDate, `${newerHour}:00`) +
      (type.id === 'hours' ? 3599 : 86399),
  };
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
      updateLogRangeControls();
      elements.logTabs.querySelectorAll('[data-log-type]').forEach((tab) => {
        const active = tab.dataset.logType === selectedLogType;
        tab.classList.toggle('active', active);
        tab.setAttribute('aria-selected', String(active));
      });
      if (restoreLogView()) {
        renderLogSummary();
        if (logHasMore) loadLogPage(false);
      }
      else {
        clearLogView();
        renderLogSummary();
        loadLogSelection();
      }
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
function updateLogFillOptions(typeId) {
  if (elements.logFillMetric.dataset.type === typeId) return;
  const options = typeId === 'alerts' ?
    [['duration', 'Duration'], ['dose', 'Dose'], ['average', 'Average'], ['maximum', 'Maximum']] :
    [['dose', 'Dose'], ['average', 'Average'], ['maximum', 'Maximum'], ['coverage', 'Coverage']];
  if (!options.some(([value]) => value === logFillMetric)) {
    logFillMetric = options[0][0];
  }
  elements.logFillMetric.replaceChildren();
  for (const [value, label] of options) {
    const option = document.createElement('option');
    option.value = value;
    option.textContent = label;
    elements.logFillMetric.append(option);
  }
  elements.logFillMetric.value = logFillMetric;
  elements.logFillMetric.dataset.type = typeId;
}
function logFillValue(record, type) {
  switch (logFillMetric) {
    case 'duration': return type.id === 'alerts' ? record.covered : 0;
    case 'average': return record.average;
    case 'maximum': return record.maximum;
    case 'coverage': return logCoveragePercent(record, type);
    default: return record.dose;
  }
}
function formatTotalDuration(seconds) {
  if (seconds >= 3600) {
    const hours = Math.floor(seconds / 3600);
    return `${hours}:${String(Math.floor(seconds / 60) % 60).padStart(2, '0')}h`;
  }
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}m`;
}
function renderLogTotal(type) {
  const totalDose = logRecords.reduce((sum, record) => sum + record.dose, 0);
  const covered = logRecords.reduce((sum, record) => sum + record.covered, 0);
  const weightedAverage = covered ? Math.round(logRecords.reduce((sum, record) =>
    sum + record.average * record.covered, 0) / covered) : 0;
  const maximum = logRecords.reduce((max, record) => Math.max(max, record.maximum), 0);
  const isCnt = (deviceSettings?.[9] ?? 0) === 1;
  const dose = isCnt ? `${formatGroupedNumber(totalDose)} CNT` :
    `${formatLogRadiation(totalDose * 0.000111).value} ${formatLogRadiation(totalDose * 0.000111).unit}`;
  const average = isCnt ? `${formatGroupedNumber(weightedAverage)} CPM` :
    `${formatLogRadiation(weightedAverage * 0.00666, true).value} ${formatLogRadiation(weightedAverage * 0.00666, true).unit}`;
  const maximumText = isCnt ? `${formatGroupedNumber(maximum)} CPM` :
    `${formatLogRadiation(maximum * 0.00666, true).value} ${formatLogRadiation(maximum * 0.00666, true).unit}`;
  const values = type.id === 'alerts' ?
    [`TOTAL (${logRecords.length})`, formatTotalDuration(covered), dose, average, maximumText] :
    [`TOTAL (${logRecords.length})`, dose, average, maximumText, `${formatGroupedNumber(covered)}s total`];
  const row = document.createElement('div');
  row.className = 'log-grid log-record log-total';
  values.forEach((value, index) => {
    const cell = document.createElement('span');
    if (index === 2 && type.id === 'alerts') cell.className = `log-metric ${logDoseClass({dose: totalDose, covered})}`;
    else if (index === (type.id === 'alerts' ? 3 : 2)) cell.className = `log-metric ${logRateClass(weightedAverage)}`;
    else if (index === (type.id === 'alerts' ? 4 : 3)) cell.className = `log-metric ${logRateClass(maximum)}`;
    else if (index === 1 && type.id !== 'alerts') cell.className = `log-metric ${logDoseClass({dose: totalDose, covered})}`;
    cell.textContent = value;
    row.append(cell);
  });
  elements.logContent.append(row);
}
function renderLogSummary() {
  elements.logContent.replaceChildren();
  updateLogRangeControls();
  elements.loadMoreLogs.hidden = !logInfo || !logHasMore;
  elements.loadMoreLogs.disabled = logPageLoading;
  if (!logInfo) return;
  const type = logTypes.find((item) => item.id === selectedLogType);
  updateLogFillOptions(type.id);
  elements.logsStatus.textContent = `${formatGroupedNumber(logRecords.length)} records loaded | ${formatGroupedNumber(logInfo[type.countKey])} stored entries${logPageLoading ? (logRangeAll ? ' | loading all logs...' : logRangeActive ? ' | loading range...' : logCountLimit ? ` | loading last ${logCountLimit}...` : '') : ''}${logInfo.memory === 2 ? ' | recording off' : ''}${logInfo.error ? ' | storage error' : ''}`;
  if (!logRecords.length) { elements.logContent.textContent = logPageLoading ? 'Loading records…' : 'No records available.'; return; }
  const header = document.createElement('div'); header.className = 'log-grid log-grid-header';
  header.innerHTML = selectedLogType === 'alerts' ?
    '<span>Date</span><span>Duration</span><span>Dose</span><span>Average</span><span>Maximum</span>' :
    '<span>Date</span><span>Dose</span><span>Average</span><span>Maximum</span><span>Coverage</span>';
  elements.logContent.append(header);
  renderLogTotal(type);
  const maximum = logRecords.reduce((largest, record) =>
    Math.max(largest, logFillValue(record, type)), 1);
  for (const record of logRecords) {
    const row = document.createElement('div');
    const isAlert = selectedLogType === 'alerts';
    row.className = 'log-grid log-record';
    row.style.setProperty('--bar-width', `${Math.min(100, logFillValue(record, type) * 100 / maximum)}%`);
    row.title = `Fill ${logFillMetric}: ${logFillValue(record, type)} · Clock period ${record.epoch}${record.live ? ' · live' : ''}${record.partial ? ' · incomplete coverage' : ''}`;
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

async function queryLogPage(type, cursor, limit, count = LOG_BATCH_SIZE) {
  // Keep transport pages at two records; larger UI batches combine requests.
  count = Math.max(1, Math.min(count, 2, Math.floor((protocolMaxPayload - 12) / 25)));
  const requestStarted = performance.now();
  logDiagnostic(`Stored-log read request: memory=${logMemoryName(logInfo?.source ?? logInfo?.memory ?? 0)} type=${type.id} period=${type.period}s count=${count} dateLimit=${limit} cursor=${cursor.time}/${cursor.epoch}/${cursor.skip}`);
  const request = new Uint8Array(18); const view = new DataView(request.buffer);
  request[0] = type.protocolId; request[1] = count;
  view.setUint32(2, type.period, true); view.setUint32(6, cursor.time, true);
  view.setUint16(10, cursor.epoch, true); view.setUint16(12, cursor.skip, true); view.setUint32(14, limit, true);
  let payload;
  try {
    payload = await serial.request(OP_GET_LOG_PAGE, request, 15000);
  } catch (error) {
    logDiagnostic(`GET_LOG_PAGE failed after ${(performance.now() - requestStarted).toFixed(1)} ms`, error);
    throw error;
  }
  if (payload.length < 12 || payload[0] !== 0 || payload[1] !== type.protocolId ||
      payload[2] > count || payload.length !== 12 + payload[2] * 25) {
    logDiagnostic(`GET_LOG_PAGE invalid response after ${(performance.now() - requestStarted).toFixed(1)} ms: bytes=${payload.length}, status=${payload[0]}, type=${payload[1]}, count=${payload[2]}, requested=${count}`);
    throw new ProtocolError('Invalid canonical log response.');
  }
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
  const nextCursor = {time: data.getUint32(4, true), epoch: data.getUint16(8, true), skip: data.getUint16(10, true)};
  logDiagnostic(`GET_LOG_PAGE response ${(performance.now() - requestStarted).toFixed(1)} ms: status=${payload[0]}, records=${records.length}, more=${!!payload[3]}, next=${nextCursor.time}/${nextCursor.epoch}/${nextCursor.skip}`);
  for (const record of records) {
    logDiagnostic(`  ${record.calendar ? 'calendar' : 'uptime'} t=${record.timestamp} epoch=${record.epoch} id=${record.id} doseCnt=${record.dose} avgCPM=${record.average} maxCPM=${record.maximum} covered=${record.covered}s flags=${Number(record.available) | (Number(record.partial) << 1) | (Number(record.live) << 2) | (Number(record.calendar) << 3)}`);
  }
  return {records, more: !!payload[3], cursor: nextCursor};
}

async function applyCalendarRange(range, mode) {
  if (!range || !Number.isFinite(range.start) || !Number.isFinite(range.end) ||
      range.start > range.end) {
    elements.logsStatus.textContent = 'Newer bound must be later than older bound.';
    return;
  }
  saveLogView(); ++logPageGeneration;
  logRangeActive = true;
  logRangeAll = false;
  logCountLimit = 0;
  logRangeStart = range.start;
  logRangeEnd = range.end;
  logSelectedDate = `${range.start}:${range.end}`;
  clearLogView();
  renderLogSummary();
  await loadLogPage(false);
}

async function loadLogSelection() {
  if (!serial || !logInfo) return;
  const mode = elements.logRangeMode.value;
  if (mode === 'all') return selectAllLogs();
  if (['5', '10', '50'].includes(mode)) return loadRecentLogs(Number(mode));
  const type = logTypes.find((item) => item.id === selectedLogType);
  if (mode === 'latest') {
    if (!logInfo.calendarValid) return loadRecentLogs(LOG_INITIAL_TARGETS[type.id]);
    setDefaultLogRange(type, true);
    return loadRecentLogs(LOG_INITIAL_TARGETS[type.id]);
  }
  if (mode !== 'latest' && mode !== 'custom') return;
  if (!logInfo.calendarValid) {
    elements.logsStatus.textContent = 'Set the device clock to load a custom calendar range.';
    return;
  }
  await applyCalendarRange(readCustomLogRange(type), mode);
}

async function selectAllLogs() {
  if (!serial || !logInfo) return;
  saveLogView();
  ++logPageGeneration;
  logRangeActive = true;
  logRangeAll = true;
  logCountLimit = 0;
  logRangeStart = 0;
  logRangeEnd = 0xffffffff;
  logSelectedDate = 'all';
  clearLogView();
  renderLogSummary();
  await loadLogPage(false);
}

async function loadRecentLogs(count) {
  if (!serial || !logInfo || !Number.isInteger(count) || count < 1) return;
  saveLogView();
  ++logPageGeneration;
  logRangeActive = false;
  logRangeAll = false;
  logCountLimit = count;
  logSelectedDate = '';
  clearLogView();
  renderLogSummary();
  await loadLogPage(false);
}

async function loadMoreLogRecords() {
  if (logCountLimit) logCountLimit += LOG_BATCH_SIZE;
  await loadLogPage(false);
}

function csvCell(value) {
  const text = String(value ?? '');
  return /[",\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

function logCsvRow(record, type) {
  const cnt = (deviceSettings?.[9] ?? 0) === 1;
  const isR = (deviceSettings?.[9] ?? 0) === 2;
  const dose = cnt ? record.dose : record.dose * 0.000111 * (isR ? 100 : 1);
  const average = cnt ? record.average : record.average * 0.00666 * (isR ? 100 : 1);
  const maximum = cnt ? record.maximum : record.maximum * 0.00666 * (isR ? 100 : 1);
  const doseUnit = cnt ? 'CNT' : isR ? 'µR' : 'µSv';
  const rateUnit = cnt ? 'CPM' : isR ? 'µR/h' : 'µSv/h';
  const coverage = type.id === 'alerts' ? '' : logCoveragePercent(record, type);
  const state = record.live ? 'live' : record.partial ? 'partial' : 'full';
  return [formatLogDate(record.timestamp, type.id, record.calendar), type.id,
    Number(dose.toPrecision(9)), doseUnit, Number(average.toPrecision(9)), rateUnit,
    Number(maximum.toPrecision(9)), record.covered, coverage, state,
    record.epoch, record.id, record.calendar ? 1 : 0];
}

function downloadLogCsv(records, filename) {
  const header = ['timestamp', 'log_type', 'dose', 'dose_unit', 'average',
    'rate_unit', 'maximum', 'covered_seconds', 'coverage_percent', 'status',
    'epoch', 'record_id', 'calendar_valid'];
  const csv = [header, ...records.map(({record, type}) => logCsvRow(record, type))]
    .map((row) => row.map(csvCell).join(',')).join('\r\n');
  const blob = new Blob(['\uFEFF', csv], {type: 'text/csv;charset=utf-8'});
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.click();
  URL.revokeObjectURL(url);
}

function exportLogsCsv() {
  if (!serial || !logInfo || logCsvBusy) return;
  const generation = logPageGeneration;
  const type = logTypes.find((item) => item.id === selectedLogType);
  const records = logRecords.slice();
  logCsvBusy = true;
  updateLogRangeControls();
  try {
    if (generation !== logPageGeneration) throw new Error('Log export cancelled because the view changed.');
    const fromName = formatLogDate(logRangeStart, type.id).replace(/[^0-9]/g, '');
    const toName = formatLogDate(logRangeEnd, type.id).replace(/[^0-9]/g, '');
    const filename = logRangeAll ? `device-logs-${type.id}-all.csv` :
      logCountLimit ? `device-logs-${type.id}-last-${logCountLimit}.csv` :
      logRangeActive ? `device-logs-${type.id}-${fromName}-to-${toName}.csv` :
        `device-logs-${type.id}-loaded.csv`;
    downloadLogCsv(records.map((record) => ({record, type})),
      filename);
    elements.logsStatus.textContent = `Exported ${formatGroupedNumber(records.length)} ${type.label.toLowerCase()} records.`;
  } catch (error) {
    elements.logsStatus.textContent = error.message;
  } finally {
    logCsvBusy = false;
    updateLogRangeControls();
  }
}

async function loadLogPage(reset = false) {
  if (!serial || !logInfo) return;
  if (logPageLoading) { if (reset) ++logPageGeneration; logPageReloadPending = true; return; }
  if (reset) { ++logPageGeneration; clearLogView(); }
  const generation = logPageGeneration, type = logTypes.find((item) => item.id === selectedLogType);
  const target = isLatestLogView() ? LOG_INITIAL_TARGETS[type.id] :
    logRangeActive ? Number.MAX_SAFE_INTEGER : logCountLimit ||
      logRecords.length + LOG_INITIAL_TARGETS[type.id];
  const limit = logDateLimit();
  logDiagnostic(`loadLogPage start type=${type.id} mode=${elements.logRangeMode.value} target=${target} limit=${limit} existing=${logRecords.length}`);
  logPageLoading = true; renderLogSummary();
  let loadFailed = false;
  const keys = new Set(logRecords.map(logRecordKey));
  let lastRenderedCount = logRecords.length;
  try {
    let crossedRangeStart = false;
    while (generation === logPageGeneration && logHasMore && logRecords.length < target) {
      const batchEnd = Math.min(target, logRecords.length + LOG_BATCH_SIZE);
      while (generation === logPageGeneration && logHasMore && logRecords.length < batchEnd) {
        const page = await queryLogPage(type, logCursor, limit, Math.min(LOG_BATCH_SIZE, batchEnd - logRecords.length));
        if (generation !== logPageGeneration) break;
        for (const record of page.records) {
          if (logRangeActive && record.calendar && record.timestamp < logRangeStart) {
            crossedRangeStart = true;
            continue;
          }
          if (logRecordInRange(record) && !keys.has(logRecordKey(record))) {
            logRecords.push(record); keys.add(logRecordKey(record));
          }
        }
        logDiagnostic(`loadLogPage batch records=${page.records.length} total=${logRecords.length}`);
        logCursor = page.cursor;
        logHasMore = page.more && page.records.length > 0 && !crossedRangeStart;
      }
      if (generation === logPageGeneration) {
        const finished = !logHasMore || logRecords.length >= target;
        if (finished || logRecords.length - lastRenderedCount >= LOG_BATCH_SIZE) {
          renderLogSummary(); saveLogView();
          lastRenderedCount = logRecords.length;
          await new Promise((resolve) => requestAnimationFrame(resolve));
        } else {
          elements.logsStatus.textContent = `${formatGroupedNumber(logRecords.length)} records loaded | ${formatGroupedNumber(logInfo[type.countKey])} stored entries | loading range...`;
        }
      }
    }
    if (isLatestLogView() && logRecords.length >= target) logHasMore = false;
  } catch (error) {
    loadFailed = true;
    logDiagnostic('loadLogPage failed', error);
    if (generation === logPageGeneration) elements.logsStatus.textContent = error.message;
  } finally {
    logPageLoading = false;
    updateLogRangeControls();
    if (!loadFailed && generation === logPageGeneration) renderLogSummary();
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
        const page = await queryLogPage(type, cursor, 0, LOG_BATCH_SIZE - batch);
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
    logDiagnostic('loadNewerLogRecords failed', error);
    if (generation === logPageGeneration) elements.logsStatus.textContent = error.message;
  } finally { logRefreshBusy = false; }
}

async function loadLogInfo() {
  if (!serial || logInfoLoading) return;
  logInfoLoading = true;
  const requestStarted = performance.now();
  try {
    logDiagnostic('GET_LOG_INFO request: read selected storage, physical counts, SPI identification, RTC and writer fault.');
    const payload = await serial.request(OP_GET_LOG_INFO, undefined, 15000);
    applyLogInfo(payload);
    if (!logTypes.some((type) => type.id === selectedLogType)) selectedLogType = 'minutes';
    logDiagnostic(`GET_LOG_INFO response ${(performance.now() - requestStarted).toFixed(1)} ms, ${payload.length} bytes: memory=${logMemoryName(logInfo.memory)}, readSource=${logMemoryName(logInfo.source)}, counts MIN=${logInfo.minutes} HOUR=${logInfo.hours} DAY=${logInfo.days} ALERT=${logInfo.alerts}, SPI-ready=${logInfo.spiReady}, size=${logInfo.spiSizeMb}MB, JEDEC=${logInfo.jedec || '—'}, writerFault=${logInfo.error}, spiFault=${spiFaultDescription(logInfo)}, RTC=${logInfo.rtcStatus}`);
    renderLogTabs(); updateSpiDumpAvailability();
    if (!logRecords.length) await loadLogSelection();
  } catch (error) {
    logDiagnostic('loadLogInfo failed', error);
    elements.logsStatus.textContent = error.message;
  }
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
    const response = await serial.request(OP_SET_RTC, request, 20000);
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
  if (elements.connectUsb) {
    elements.connectUsb.hidden = !webUsbAvailable || canDisconnect;
    elements.connectUsb.disabled = state === 'connecting' || canDisconnect;
  }
  if (elements.bridgePort) elements.bridgePort.disabled = state === 'connecting' || canDisconnect;
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
  protocolMaxPayload = Math.min(MAX_PAYLOAD, maxPayload);
  updateRtcControls();
  updateSpiDumpAvailability();
}

function updateSpiDumpAvailability() {
  if (!elements.dumpSpi) return;
  elements.dumpSpi.disabled = dumpBusy || !!spiRecoveryBackup;
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

function makeSettingVisibilityControl(settingId, supported, preview) {
  const label = document.createElement('label');
  label.className = 'setting-visible';
  const input = document.createElement('input');
  input.type = 'checkbox';
  input.checked = preview ? !demoHiddenSettings.has(settingId) :
    deviceHiddenSettings === null || (deviceHiddenSettings[settingId] ?? false) === false;
  input.disabled = !preview && (!supported || deviceHiddenSettings === null);
  input.title = 'Show this setting on the device';
  input.addEventListener('change', () => {
    if (preview) {
      if (input.checked) demoHiddenSettings.delete(settingId);
      else demoHiddenSettings.add(settingId);
      return;
    }
    writeSetting(0x80 + settingId, input.checked ? 0 : 1, input);
  });
  const text = document.createElement('span');
  text.textContent = 'Show';
  label.append(input, text);
  return label;
}

function renderSettings() {
  elements.settingsList.replaceChildren();
  const preview = deviceSettings === null;
  const currentSettings = preview ? demoSettings : deviceSettings;
  const rows = [];
  for (const setting of remoteSettings) {
    if (setting.id === 19) continue;
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
    if (setting.id === 13 || isReset) row.classList.add('setting-danger');
    if (isPowerOff) {
      row.classList.add('setting-action-warning', 'setting-power-actions');
      const actionTitle = document.createElement('span');
      actionTitle.className = 'setting-action-title';
      actionTitle.textContent = 'POWER';
      row.append(actionTitle);
      const powerOffButton = makeDeviceActionButton('POWER OFF', 1);
      const rebootButton = makeDeviceActionButton('REBOOT', 2);
      const rebootSupported = supportSettings !== null && 19 < supportSettings.length &&
        (!serial || (protocolCapabilities & CAP_DEVICE_ACTION) !== 0);
      for (const [button, actionIsSupported] of [
        [powerOffButton, supported], [rebootButton, rebootSupported]
      ]) {
        if (!actionIsSupported || preview) {
          button.disabled = true;
          button.title = preview ? 'Connect a device to enable this action.' :
            'This device firmware does not support this action.';
        }
      }
      controls.classList.add('setting-power-controls');
      controls.append(powerOffButton, rebootButton,
        makeSettingVisibilityControl(19, rebootSupported, preview));
      row.append(controls);
      rows.push(row);
      continue;
    }

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
      select.addEventListener('change', async () => {
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
          if (setting.id === 13 && nextValue === 1 && deviceSettings[13] !== 1) {
            const accepted = await confirmSpiEnable();
            if (!accepted) {
              select.value = String(deviceSettings[13]);
              return;
            }
          }
          writeSetting(setting.id, nextValue, select);
        }
      });
      controls.append(select);
    }

    controls.append(makeSettingVisibilityControl(setting.id, supported, preview));
    row.append(label);
    row.append(controls);
    rows.push(row);
  }
  const pair = document.createElement('div');
  pair.className = 'settings-pair';
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
  elements.debugRtcClock.textContent = 'NOT SET LSI';
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
  elements.debugSpi.textContent = '1 MB';
  elements.debugSpi.classList.remove('state-error');
  elements.debugJedec.textContent = '—';
  elements.debugSeconds.textContent = '0';
  elements.rateChart.setAttribute('aria-label', 'Illustrative demo graph, not device data');
  elements.logsStatus.textContent = 'Demo preview · no device logs loaded';
  const demoLogTypes = logTypes.map(({ id, label }) => [id, label]);
  elements.logTabs.replaceChildren();
  elements.logContent.replaceChildren();
  const renderDemoLog = (type) => {
    selectedLogType = type;
    updateLogRangeControls();
    elements.logTabs.querySelectorAll('[data-demo-log]').forEach((tab) =>
      tab.classList.toggle('active', tab.dataset.demoLog === type));
    updateLogFillOptions(type);
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
    const demoRecord = {dose: 0, average: 0, maximum: 0,
      covered: alert ? 0 : demoType.period, live: false, partial: false};
    const fillValue = logFillValue(demoRecord, demoType);
    record.style.setProperty('--bar-width', `${fillValue ? 100 : 0}%`);
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
  demoLogRenderer = renderDemoLog;
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
  updateLogRangeControls();
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
    if (result.length !== 1 || result[0] !== 0) {
      throw new ProtocolError(settingId === 13 ?
        'Log memory change failed; the device kept the previous mode. Check SPI status on the Debug page.' :
        'Device rejected this setting.');
    }
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
    'Clear all log records in EEPROM and SPI? Erasing SPI also removes stock assets, so stock firmware may no longer work.',
    deviceSettings?.[13] === 1 ?
      'Clear inactive EEPROM logs? Active SPI logs will remain.' :
      deviceSettings?.[13] === 0 ?
        'Clear inactive SPI logs? This erases the entire SPI chip, including stock assets; stock firmware may no longer work.' :
        'LOG MEMORY is OFF: clear all logs in EEPROM and SPI? SPI erasure also removes stock assets.',
    'Erase the entire EEPROM, including settings and logs? The device will restart.',
    'Erase the entire SPI chip, including assets needed by stock firmware? This cannot be undone.',
    'Erase all EEPROM and SPI data, including stock assets? This cannot be undone; the device will restart.',
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
    if (action === 0 && ![0, 3, 5].includes(value)) {
      logInfo = null;
      logRecords = [];
      logViewCache.clear(); logTypeActiveView.clear(); logSelectedDate = ""; logRangeActive = false; logRangeAll = false; logCountLimit = 0; logRangeStart = 0; logRangeEnd = 0; logRangeDefaults.clear(); logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
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

async function refreshSettingsPage() {
  const button = document.querySelector('#settings-refresh');
  if (!serial) {
    elements.settingsStatus.textContent = 'Connect to refresh device data.';
    return;
  }
  button.disabled = true;
  try {
    await loadSettings();
    await loadLogInfo();
  } finally {
    button.disabled = false;
  }
}

async function refreshDebugPage() {
  const button = document.querySelector('#debug-refresh');
  if (!serial) {
    showNotice('Connect to refresh device data.', '');
    return;
  }
  button.disabled = true;
  try {
    await refreshStatus();
    await loadLogInfo();
  } finally {
    button.disabled = false;
  }
}

async function requestHelloAfterBoot(protocol) {
  // WebUSB leaves modem-control lines untouched, so it does not trigger reset.
  await delay(protocol.port instanceof Cp210xUsbPort ? 100 : 1000);
  const timeout = protocol.port instanceof Cp210xUsbPort ? 3500 : 1800;
  let lastError;
  for (let attempt = 0; attempt < 4; attempt += 1) {
    try {
      return await protocol.request(OP_HELLO, new Uint8Array(), timeout);
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
document.querySelector('#settings-refresh').addEventListener('click', refreshSettingsPage);
document.querySelector('#debug-refresh').addEventListener('click', refreshDebugPage);
elements.syncClock.addEventListener('click', synchronizeDeviceClock);
elements.logsTab.addEventListener('click', () => selectPage('logs'));
elements.loadMoreLogs.addEventListener('click', loadMoreLogRecords);
elements.loadLogSelection.addEventListener('click', loadLogSelection);
elements.clearLogDiagnostics.addEventListener('click', () => {
  logDiagnostics.length = 0;
  elements.logDiagnostics.textContent = '';
});
elements.copyLogDiagnostics.addEventListener('click', async () => {
  try {
    await navigator.clipboard.writeText(logDiagnostics.join('\n'));
    logDiagnostic('Diagnostics copied to clipboard.');
  } catch (error) {
    logDiagnostic('Could not copy diagnostics', error);
  }
});
elements.logRangeMode.addEventListener('change', updateLogRangeControls);
elements.logFillMetric.addEventListener('change', () => {
  logFillMetric = elements.logFillMetric.value;
  if (serial && logInfo) renderLogSummary();
  else demoLogRenderer?.(selectedLogType);
});
elements.exportLogsCsv.addEventListener('click', exportLogsCsv);
elements.firmwareTab.addEventListener('click', () => selectPage('firmware'));
elements.rebootFirmware.addEventListener('click', rebootToFirmware);
elements.rebootBootloader.addEventListener('click', rebootToBootloader);
elements.firmwareFile.addEventListener('change', () => loadFirmwareImage());
elements.flashLocalBuild.addEventListener('click', flashLocalBuild);
elements.quickFlashFirmware?.addEventListener('click', quickFlashEmbeddedFirmware);
elements.clearFirmware.addEventListener('click', clearFirmwareSelection);
elements.flashFirmware.addEventListener('click', flashSelectedFirmware);
elements.dumpFlash.addEventListener('click', () => dumpRomMemory(
  'FLASH', STM32_FLASH_BASE, STM32_FLASH_SIZE, 'flash'));
elements.quickBackupFlash?.addEventListener('click', () => elements.dumpFlash.click());
elements.quickRestoreFlash?.addEventListener('click', restoreLatestFlashBackup);
elements.firmwareRestoreFlash?.addEventListener('click', restoreLatestFlashBackup);
elements.eepromRangePreset.addEventListener('change', () => applyMemoryPreset('eeprom'));
elements.eepromAddressFrom.addEventListener('input', () => updateMemoryPresetSelection('eeprom'));
elements.eepromAddressTo.addEventListener('input', () => updateMemoryPresetSelection('eeprom'));
elements.spiRangePreset.addEventListener('change', () => applyMemoryPreset('spi'));
elements.spiAddressFrom.addEventListener('input', () => updateMemoryPresetSelection('spi'));
elements.spiAddressTo.addEventListener('input', () => updateMemoryPresetSelection('spi'));
elements.dumpEeprom.addEventListener('click', () => {
  try {
    const range = getMemoryRange('eeprom');
    void dumpRomMemory('EEPROM', STM32_EEPROM_BASE + range.start, range.length,
      `eeprom-${range.start}-${range.end}`);
  } catch (error) { setDumpStatus(error.message, 'error'); }
});
elements.dumpSpi.addEventListener('click', () => {
  try { void dumpSpiMemory(getMemoryRange('spi')); }
  catch (error) { setDumpStatus(error.message, 'error'); }
});
elements.eraseEeprom.addEventListener('click', () => {
  try {
    const range = getMemoryRange('eeprom');
    void writeEepromImage({
      name: `EEPROM erase ${range.start}-${range.end}`,
      bytes: new Uint8Array(range.length).fill(0xff)
    }, 'Erase', true);
  } catch (error) { setDumpStatus(error.message, 'error'); }
});
elements.eraseSpi.addEventListener('click', () => {
  try {
    const range = getMemoryRange('spi');
    void writeSpiImage({
      name: `SPI erase ${range.start}-${range.end}`,
      bytes: new Uint8Array(range.length).fill(0xff)
    }, 'Erase', true);
  } catch (error) { setDumpStatus(error.message, 'error'); }
});
elements.downloadSpiBackup?.addEventListener('click', downloadSpiRecoveryBackup);
elements.eepromImageFile.addEventListener('change', loadEepromImage);
elements.clearEepromImage.addEventListener('click', clearEepromImageSelection);
elements.writeEepromImage.addEventListener('click', writeEepromImage);
elements.spiImageFile.addEventListener('change', loadSpiImage);
elements.clearSpiImage.addEventListener('click', clearSpiImageSelection);
elements.writeSpiImage.addEventListener('click', writeSpiImage);
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
loadLatestFlashBackup();
if (typeof globalThis.EMBEDDED_FIRMWARE_B64 === 'string' && elements.embeddedQuickFlash) {
  elements.embeddedQuickFlash.hidden = false;
  elements.embeddedQuickFlash.open = true;
  elements.firmwareTools.open = false;
}

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

async function quickFlashEmbeddedFirmware() {
  if (firmwareFlashBusy || typeof globalThis.EMBEDDED_FIRMWARE_B64 !== 'string') return;
  if (!window.confirm('Quick flash will erase and replace the device firmware with the image embedded in this app. Continue?')) return;
  try {
    const binary = atob(globalThis.EMBEDDED_FIRMWARE_B64);
    const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
    const image = new File([bytes], 'embedded-firmware.bin', { type: 'application/octet-stream' });
    await loadFirmwareImage(image);
    if (!selectedFirmwareImage) throw new Error('The embedded firmware failed image validation.');
    await flashSelectedFirmware();
  } catch (error) {
    appendFirmwareLog(`Quick flash failed: ${error.message}`, 'error');
    setRebootStatus(`Quick flash failed: ${error.message}`, 'error');
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
  if (elements.quickBackupFlash) elements.quickBackupFlash.disabled = busy;
  if (elements.quickRestoreFlash) elements.quickRestoreFlash.disabled = busy || firmwareFlashBusy || !latestFlashBackup;
  elements.dumpEeprom.disabled = busy;
  elements.eraseEeprom.disabled = busy;
  elements.eepromRangePreset.disabled = busy;
  elements.eepromAddressFrom.disabled = busy;
  elements.eepromAddressTo.disabled = busy;
  elements.eraseSpi.disabled = busy;
  elements.spiRangePreset.disabled = busy;
  elements.spiAddressFrom.disabled = busy;
  elements.spiAddressTo.disabled = busy;
  elements.eepromImageFile.disabled = busy;
  elements.clearEepromImage.disabled = busy;
  elements.spiImageFile.disabled = busy;
  elements.clearSpiImage.disabled = busy;
  elements.rebootFirmware.disabled = busy;
  elements.rebootBootloader.disabled = busy;
  elements.flashFirmware.disabled = busy || firmwareFlashBusy || !selectedFirmwareImage;
  if (elements.quickFlashFirmware) elements.quickFlashFirmware.disabled = busy;
  if (elements.flashLocalBuild) elements.flashLocalBuild.disabled = busy;
  if (elements.firmwareRestoreFlash) elements.firmwareRestoreFlash.disabled = busy || firmwareFlashBusy || !latestFlashBackup;
  if (elements.downloadSpiBackup) elements.downloadSpiBackup.disabled = busy;
  updateEepromWriteButton();
  updateSpiWriteButton();
  updateSpiDumpAvailability();
}

function updateEepromWriteButton() {
  let fits = false;
  try {
    if (selectedEepromImage) {
      getMemoryWriteStart('eeprom', selectedEepromImage.bytes.length);
      fits = true;
    }
  }
  catch (_) { }
  elements.writeEepromImage.disabled = dumpBusy || firmwareFlashBusy || !fits;
}

function updateSpiWriteButton() {
  let fits = false;
  try {
    if (selectedSpiImage) {
      getMemoryWriteStart('spi', selectedSpiImage.bytes.length);
      fits = true;
    }
  }
  catch (_) { }
  elements.writeSpiImage.disabled = dumpBusy || firmwareFlashBusy || !fits || !!spiRecoveryBackup;
}

function memoryRangeControls(memory) {
  return memory === 'eeprom'
    ? { preset: elements.eepromRangePreset, from: elements.eepromAddressFrom,
        to: elements.eepromAddressTo, size: STM32_EEPROM_SIZE }
    : { preset: elements.spiRangePreset, from: elements.spiAddressFrom,
        to: elements.spiAddressTo, size: SPI_NOR_SIZE };
}

function applyMemoryPreset(memory) {
  const controls = memoryRangeControls(memory);
  const range = MEMORY_PRESETS[memory][controls.preset.value];
  if (!range) return;
  controls.from.value = String(range[0]);
  controls.to.value = String(range[1]);
  memory === 'eeprom' ? updateEepromWriteButton() : updateSpiWriteButton();
}

function updateMemoryPresetSelection(memory) {
  const controls = memoryRangeControls(memory);
  const matching = Object.entries(MEMORY_PRESETS[memory]).find(([, range]) =>
    Number(controls.from.value) === range[0] && Number(controls.to.value) === range[1]);
  controls.preset.value = matching?.[0] || 'custom';
  memory === 'eeprom' ? updateEepromWriteButton() : updateSpiWriteButton();
}

function getMemoryRange(memory) {
  const controls = memoryRangeControls(memory);
  if (!controls.from.value.trim() || !controls.to.value.trim())
    throw new Error(`Enter both ${memory.toUpperCase()} address offsets.`);
  const start = Number(controls.from.value);
  const end = Number(controls.to.value);
  if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) ||
      start < 0 || end < start || end >= controls.size) {
    throw new Error(`Enter an inclusive ${memory.toUpperCase()} address range from 0 to ${controls.size - 1}.`);
  }
  return { start, end, length: end - start + 1 };
}

function getMemoryWriteStart(memory, length) {
  const controls = memoryRangeControls(memory);
  if (!controls.from.value.trim()) throw new Error(`Enter the ${memory.toUpperCase()} From address.`);
  const start = Number(controls.from.value);
  if (!Number.isSafeInteger(start) || start < 0 || start >= controls.size ||
      !Number.isSafeInteger(length) || length < 1 || length > controls.size - start) {
    throw new Error(`The image does not fit in ${memory.toUpperCase()} from the selected From address.`);
  }
  return start;
}

function clearEepromImageSelection() {
  selectedEepromImage = null;
  elements.eepromImageFile.value = '';
  elements.eepromImageStatus.textContent = 'No image selected.';
  elements.eepromImageStatus.dataset.kind = 'unknown';
  elements.eepromWriteStatus.textContent = 'Select an EEPROM image that fits from the From address. Read-back verification is always enabled.';
  elements.eepromWriteStatus.dataset.level = '';
  updateEepromWriteButton();
}

function clearSpiImageSelection() {
  selectedSpiImage = null;
  elements.spiImageFile.value = '';
  elements.spiImageStatus.textContent = 'No image selected.';
  elements.spiImageStatus.dataset.kind = 'unknown';
  elements.spiWriteStatus.textContent = 'Select an SPI image that fits from the From address. Read-back verification is always enabled.';
  elements.spiWriteStatus.dataset.level = '';
  updateSpiWriteButton();
}

async function loadSpiImage() {
  selectedSpiImage = null;
  elements.writeSpiImage.disabled = true;
  const file = elements.spiImageFile.files?.[0];
  if (!file) {
    clearSpiImageSelection();
    return;
  }
  try {
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (bytes.length === 0 || bytes.length > SPI_NOR_SIZE) {
      throw new Error(`Expected 1 to ${SPI_NOR_SIZE.toLocaleString()} bytes; selected file has ${bytes.length.toLocaleString()}.`);
    }
    selectedSpiImage = {name: file.name, bytes};
    elements.spiImageStatus.textContent = `${file.name} (${bytes.length.toLocaleString()} bytes)`;
    elements.spiImageStatus.dataset.kind = 'unknown';
    elements.spiWriteStatus.textContent = `Image ready. It will be written at From=${elements.spiAddressFrom.value}; other bytes in affected sectors will be preserved.`;
    elements.spiWriteStatus.dataset.level = 'warning';
    updateSpiWriteButton();
  } catch (error) {
    elements.spiImageStatus.textContent = error.message;
    elements.spiImageStatus.dataset.kind = 'invalid';
    elements.spiWriteStatus.textContent = 'SPI image is invalid and cannot be written.';
    elements.spiWriteStatus.dataset.level = 'error';
  }
}

async function loadEepromImage() {
  selectedEepromImage = null;
  elements.writeEepromImage.disabled = true;
  const file = elements.eepromImageFile.files?.[0];
  if (!file) {
    elements.eepromImageStatus.textContent = 'No image selected.';
    elements.eepromImageStatus.dataset.kind = 'unknown';
    elements.eepromWriteStatus.textContent = 'Select an EEPROM image that fits from the From address. Read-back verification is always enabled.';
    elements.eepromWriteStatus.dataset.level = '';
    return;
  }
  try {
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (bytes.length === 0 || bytes.length > STM32_EEPROM_SIZE) {
      throw new Error(`Expected 1 to ${STM32_EEPROM_SIZE.toLocaleString()} bytes; selected file has ${bytes.length.toLocaleString()}.`);
    }
    selectedEepromImage = { name: file.name, bytes };
    elements.eepromImageStatus.textContent = `${file.name} (${bytes.length.toLocaleString()} bytes)`;
    elements.eepromImageStatus.dataset.kind = 'ours';
    elements.eepromWriteStatus.textContent = `Image ready. It will be written at From=${elements.eepromAddressFrom.value}; bytes outside the image range are preserved.`;
    elements.eepromWriteStatus.dataset.level = 'warning';
    updateEepromWriteButton();
  } catch (error) {
    elements.eepromImageStatus.textContent = error.message;
    elements.eepromImageStatus.dataset.kind = 'invalid';
    elements.eepromWriteStatus.textContent = 'EEPROM image is invalid and cannot be written.';
    elements.eepromWriteStatus.dataset.level = 'error';
  }
}

function setDumpStatus(message, level = '') {
  elements.dumpStatus.textContent = message;
  elements.dumpStatus.dataset.level = level;
}

async function selectFirmwarePort() {
  if (serial?.port) {
    activeTransport = serial.port instanceof NativeBridgePort ? 'native' :
      (serial.port instanceof Cp210xUsbPort ? 'webusb' : 'serial');
    return { port: serial.port, transport: activeTransport };
  }
  if (activeTransport === 'native' && selectedNativePort) {
    return { port: selectedNativePort, transport: 'native' };
  }
  if (activeTransport === 'webusb' && selectedUsbPort) {
    return { port: selectedUsbPort, transport: 'webusb' };
  }
  if (nativeBridgeAvailable && selectedBridgePort) {
    const port = selectedNativePort || new NativeBridgePort(selectedBridgePort);
    selectedNativePort = port;
    selectedUsbPort = null;
    selectedSerialPort = null;
    activeTransport = 'native';
    return { port, transport: 'native' };
  }
  if (androidUsbAvailable) {
    const port = selectedUsbPort || new Cp210xUsbPort();
    selectedUsbPort = port;
    selectedSerialPort = null;
    activeTransport = 'webusb';
    return { port, transport: 'webusb' };
  }
  const port = selectedSerialPort || await navigator.serial.requestPort();
  selectedSerialPort = port;
  selectedUsbPort = null;
  activeTransport = 'serial';
  return { port, transport: 'serial' };
}

async function dumpRomMemory(label, address, length, filename) {
  if (dumpBusy || firmwareFlashBusy) return;
  let port = null;
  try {
    ({ port } = await selectFirmwarePort());
  } catch (error) {
    setDumpStatus(`Could not select the USB port: ${error.message}`, 'error');
    return;
  }
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
  let backupWarning = '';
  if (label === 'FLASH') {
    try {
      await saveLatestFlashBackup(bytes, dumpFilename, false);
      setSpiRecoveryBackup(null, '');
    }
    catch (error) {
      backupWarning = ` IndexedDB backup failed: ${error.message}`;
      appendFirmwareLog(`FLASH dump downloaded but could not be saved in IndexedDB: ${error.message}`, 'warning');
    }
  }
  setDumpStatus(restartFailure
    ? `${label} dump saved as ${dumpFilename}, but firmware restart failed: ${restartFailure.message}${backupWarning}`
    : `${label} dump saved as ${dumpFilename} (${length.toLocaleString()} bytes).${backupWarning}`,
  restartFailure || backupWarning ? 'warning' : 'success');
}

async function writeEepromImage(imageOverride = null, operationLabel = 'Write', isErase = false) {
  if (dumpBusy || firmwareFlashBusy || (!selectedEepromImage && !imageOverride)) return;
  const image = imageOverride || selectedEepromImage;
  let targetStart;
  try { targetStart = getMemoryWriteStart('eeprom', image.bytes.length); }
  catch (error) {
    elements.eepromWriteStatus.textContent = error.message;
    elements.eepromWriteStatus.dataset.level = 'error';
    return;
  }
  const targetEnd = targetStart + image.bytes.length;
  const alignedStart = targetStart & ~3;
  const alignedEnd = (targetEnd + 3) & ~3;
  const preserved = new Uint8Array(alignedEnd - alignedStart);
  if (!window.confirm(
    `${operationLabel} ${image.bytes.length.toLocaleString()} EEPROM bytes from address ${targetStart} through ${targetEnd - 1}? Data outside this range will be preserved. Continue only if you have a backup.`)) return;

  let port = null;
  let session = null;
  let attemptedBootloader = false;
  let wasConnected = !!serial;
  let failure = null;
  let restartFailure = null;
  setDumpBusy(true);
  elements.eepromWriteStatus.textContent = 'Opening STM32 ROM bootloader…';
  elements.eepromWriteStatus.dataset.level = '';
  setConnection('connecting', 'Entering bootloader…');
  appendFirmwareLog(`EEPROM ${operationLabel.toLowerCase()} requested at offsets ${targetStart}-${targetEnd - 1} (${image.bytes.length.toLocaleString()} bytes).`);
  try {
    ({ port } = await selectFirmwarePort());
    wasConnected = !!serial;
    attemptedBootloader = true;
    session = await openBootloaderForImage('hex');
    const deviceId = await session.client.getDeviceId();
    if (deviceId !== STM32_DEVICE_ID) {
      throw new Error(`Expected STM32L051 device ID 0x${STM32_DEVICE_ID.toString(16)}, received 0x${deviceId.toString(16)}.`);
    }
    if (!session.info.commands.includes(0x31) || !session.info.commands.includes(0x11)) {
      throw new Error('The ROM bootloader must support Write Memory and Read Memory.');
    }

    for (let offset = 0; offset < preserved.length; offset += 128) {
      const count = Math.min(128, preserved.length - offset);
      preserved.set(await session.client.readMemory(
        STM32_EEPROM_BASE + alignedStart + offset, count), offset);
    }
    preserved.set(image.bytes, targetStart - alignedStart);

    // ROM writes require aligned addresses and lengths; preserve partial boundary words.
    for (let offset = 0; offset < preserved.length; offset += 128) {
      const block = preserved.subarray(offset, offset + 128);
      await session.client.writePage(STM32_EEPROM_BASE + alignedStart + offset, block);
      const completed = offset + block.length;
      const percent = Math.floor((completed * 100) / preserved.length);
      elements.eepromWriteStatus.textContent = `Writing EEPROM ${percent}%`;
      if (completed % 512 === 0 || completed === preserved.length)
        appendFirmwareLog(`EEPROM programmed through offset ${alignedStart + completed}.`);
    }

    elements.eepromWriteStatus.textContent = 'Verifying EEPROM read-back...';
    for (let offset = 0; offset < preserved.length; offset += 128) {
      const expected = preserved.subarray(offset, offset + 128);
      const actual = await session.client.readMemory(
        STM32_EEPROM_BASE + alignedStart + offset, expected.length);
      for (let i = 0; i < expected.length; i += 1) {
        if (actual[i] !== expected[i])
          throw new Error(`EEPROM verification mismatch at 0x${(STM32_EEPROM_BASE + alignedStart + offset + i).toString(16)}.`);
      }
      const completed = offset + expected.length;
      if (completed % 512 === 0 || completed === preserved.length)
        appendFirmwareLog(`EEPROM verified through offset ${alignedStart + completed}.`);
    }
    appendFirmwareLog(`EEPROM ${operationLabel.toLowerCase()} range written and read-back verified.`, 'success');
  } catch (error) {
    failure = error;
    appendFirmwareLog(`EEPROM write failed: ${error.message}`, 'error');
  } finally {
    if (session) await session.client.close(false);
    bootloaderPortOpen = false;
    if (attemptedBootloader && port) {
      try {
        await sendFirmwareStartSignal(port);
        appendFirmwareLog('Firmware-start signal sent after EEPROM write.');
      } catch (error) {
        restartFailure = new Error(`Could not restart firmware: ${error.message}`);
        appendFirmwareLog(restartFailure.message, 'error');
      }
    }
    if (wasConnected && port && !portMayBeOpen) {
      try { await connectDevice(port); }
      catch (error) { appendFirmwareLog(`Reconnect after EEPROM write failed: ${error.message}`, 'warning'); }
    } else if (!portMayBeOpen) {
      setConnection('disconnected', 'Disconnected');
    } else {
      setConnection('disconnect-pending', 'Disconnect required');
    }
    setDumpBusy(false);
  }

  if (failure) {
    elements.eepromWriteStatus.textContent = `EEPROM write failed: ${failure.message}`;
    elements.eepromWriteStatus.dataset.level = 'error';
  } else if (restartFailure) {
    elements.eepromWriteStatus.textContent = `EEPROM was written and verified, but firmware restart failed: ${restartFailure.message}`;
    elements.eepromWriteStatus.dataset.level = 'warning';
  } else {
    elements.eepromWriteStatus.textContent = `EEPROM range ${targetStart}-${targetEnd - 1} ${isErase ? 'erased' : 'written'} and verified; surrounding bytes preserved.`;
    elements.eepromWriteStatus.dataset.level = 'success';
  }
}

async function readSpiThroughProtocol(connection, length, startAddress = 0) {
  const bytes = new Uint8Array(length);
  let nextProgress = 10;
  for (let offset = 0; offset < length; offset += SPI_DUMP_CHUNK_SIZE) {
    const count = Math.min(SPI_DUMP_CHUNK_SIZE, length - offset);
    const request = new Uint8Array(5);
    const address = startAddress + offset;
    new DataView(request.buffer).setUint32(0, address, true);
    request[4] = count;
    const response = await connection.request(OP_GET_SPI_DUMP_PAGE, request, 10000);
    if (response.length !== count + 6 || response[0] !== 0 ||
        new DataView(response.buffer, response.byteOffset, response.byteLength).getUint32(1, true) !== address ||
        response[5] !== count) throw new ProtocolError(`Invalid SPI dump response at offset 0x${offset.toString(16)}.`);
    bytes.set(response.subarray(6), offset);
    const percent = Math.floor(((offset + count) * 100) / length);
    if (percent >= nextProgress) {
      appendFirmwareLog(`SPI read ${percent}%.`);
      nextProgress = Math.floor(percent / 10 + 1) * 10;
    }
  }
  return bytes;
}

function getEmbeddedSpiMiniImage() {
  if (spiMiniImageCache) return spiMiniImageCache;
  if (typeof globalThis.EMBEDDED_SPI_MINI_B64 !== 'string') {
    throw new Error('This app build does not include the SPI service firmware. Download the latest standalone app.');
  }
  const binary = atob(globalThis.EMBEDDED_SPI_MINI_B64);
  const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
  if (!bytes.length || bytes.length >= STM32_FLASH_SIZE) throw new Error('The embedded SPI service image has an invalid size.');
  const image = new Uint8Array(STM32_FLASH_SIZE).fill(0xff);
  const present = new Uint8Array(STM32_FLASH_SIZE);
  image.set(bytes);
  present.fill(1, 0, bytes.length);
  spiMiniImageCache = { bytes, pages: validateFirmwareImage(image, present) };
  return spiMiniImageCache;
}

function openSpiRecoveryDatabase() {
  return new Promise((resolve, reject) => {
    if (!globalThis.indexedDB) return reject(new Error('Persistent browser storage is unavailable.'));
    const request = indexedDB.open(SPI_RECOVERY_DB, 1);
    request.onupgradeneeded = () => {
      if (!request.result.objectStoreNames.contains('backup')) request.result.createObjectStore('backup');
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('Could not open backup storage.'));
  });
}

function setSpiRecoveryBackup(bytes, name) {
  spiRecoveryBackup = bytes ? {bytes, name} : null;
  if (elements.spiRecoveryControls) elements.spiRecoveryControls.hidden = !spiRecoveryBackup;
  if (elements.downloadSpiBackup) elements.downloadSpiBackup.hidden = !spiRecoveryBackup;
  if (spiRecoveryBackup && elements.firmwareTools) elements.firmwareTools.open = true;
  updateSpiWriteButton();
  updateSpiDumpAvailability();
}

function setLatestFlashBackup(bytes, name) {
  latestFlashBackup = bytes ? {bytes, name} : null;
  const available = !!latestFlashBackup;
  if (elements.quickRestoreFlash) elements.quickRestoreFlash.hidden = !available;
  if (elements.firmwareRestoreFlash) elements.firmwareRestoreFlash.hidden = !available;
}

async function saveLatestFlashBackup(bytes, name, pendingRecovery = false) {
  const db = await openSpiRecoveryDatabase();
  try {
    await new Promise((resolve, reject) => {
      const transaction = db.transaction('backup', 'readwrite');
      transaction.objectStore('backup').put({bytes: bytes.slice().buffer, name, pendingRecovery}, 'latest');
      transaction.oncomplete = resolve;
      transaction.onerror = () => reject(transaction.error || new Error('Could not save FLASH backup.'));
      transaction.onabort = () => reject(transaction.error || new Error('FLASH backup save was aborted.'));
    });
  } finally { db.close(); }
  setLatestFlashBackup(bytes, name);
}

async function loadLatestFlashBackup() {
  try {
    const db = await openSpiRecoveryDatabase();
    const item = await new Promise((resolve, reject) => {
      const request = db.transaction('backup', 'readonly').objectStore('backup').get('latest');
      request.onsuccess = () => resolve(request.result || null);
      request.onerror = () => reject(request.error);
    });
    db.close();
    if (item?.bytes?.byteLength === STM32_FLASH_SIZE) {
      const bytes = new Uint8Array(item.bytes);
      const name = item.name || 'flash-backup.bin';
      setLatestFlashBackup(bytes, name);
      if (item.pendingRecovery !== false) {
        setSpiRecoveryBackup(bytes, name);
        setDumpStatus('A pending FLASH recovery backup is available. Restore it before using the device.', 'warning');
      }
    }
  } catch { /* In-memory recovery remains available during this page session. */ }
}

async function clearSpiRecoveryBackup() {
  setSpiRecoveryBackup(null, '');
  try {
    const db = await openSpiRecoveryDatabase();
    await new Promise((resolve, reject) => {
      const transaction = db.transaction('backup', 'readwrite');
      const store = transaction.objectStore('backup');
      const request = store.get('latest');
      request.onsuccess = () => {
        if (request.result?.bytes) store.put({...request.result, pendingRecovery: false}, 'latest');
      };
      transaction.oncomplete = resolve;
      transaction.onerror = () => reject(transaction.error);
    });
    db.close();
    if (latestFlashBackup?.bytes) setLatestFlashBackup(latestFlashBackup.bytes, latestFlashBackup.name);
  } catch { /* The verified restore is complete. */ }
}

function downloadSpiRecoveryBackup() {
  if (spiRecoveryBackup?.bytes) downloadDump(spiRecoveryBackup.bytes,
    spiRecoveryBackup.name || makeDumpFilename('flash-recovery'));
}

async function readFullFlashBackup(port) {
  let session = null;
  let backup = null;
  try {
    session = await openBootloaderForImage('hex');
    const deviceId = await session.client.getDeviceId();
    if (deviceId !== STM32_DEVICE_ID) throw new Error(`Unexpected STM32 ID 0x${deviceId.toString(16)}.`);
    if (!session.info.commands.includes(0x11) || !session.info.commands.includes(0x31) ||
        (!session.info.commands.includes(0x44) && !session.info.commands.includes(0x43))) {
      throw new Error('The ROM bootloader must support FLASH read, erase, and write for safe recovery.');
    }
    backup = new Uint8Array(STM32_FLASH_SIZE);
    for (let offset = 0; offset < STM32_FLASH_SIZE; offset += 256) {
      backup.set(await session.client.readMemory(STM32_FLASH_BASE + offset, 256), offset);
      if ((offset & 4095) === 3840) appendFirmwareLog(`Recovery FLASH backup read ${offset + 256}/${STM32_FLASH_SIZE} bytes.`);
    }
    validateFirmwareImage(backup, new Uint8Array(STM32_FLASH_SIZE).fill(1));
    const name = makeDumpFilename('flash-recovery');
    setLatestFlashBackup(backup, name);
    setSpiRecoveryBackup(backup, name);
    downloadDump(backup, name);
    try {
      await saveLatestFlashBackup(backup, name, true);
      setSpiRecoveryBackup(backup, name);
    }
    catch (error) {
      appendFirmwareLog(`Persistent backup unavailable (${error.message}); requesting an external backup confirmation.`, 'warning');
      if (!window.confirm('This browser cannot retain a recovery backup after the page closes. Confirm that the downloaded 64 KB FLASH backup is saved somewhere safe before continuing.')) {
        throw new Error('SPI service was cancelled because a persistent or externally saved FLASH backup is required.');
      }
    }
    appendFirmwareLog(`Verified full FLASH recovery backup captured (${STM32_FLASH_SIZE.toLocaleString()} bytes).`, 'success');
  } finally {
    if (session) {
      await session.client.close(false);
      bootloaderPortOpen = false;
      if (portMayBeOpen) {
        try { await port.close(); portMayBeOpen = false; } catch { portMayBeOpen = true; }
      }
      if (!portMayBeOpen) await sendFirmwareStartSignal(port);
    }
  }
  if (!backup) throw new Error('Could not capture a FLASH recovery backup.');
  return backup;
}

async function programFlashImage(port, pages, label, fullImage = false) {
  let session = null;
  let programmed = false;
  let startedByGo = false;
  try {
    session = await openBootloaderForImage('bin');
    const deviceId = await session.client.getDeviceId();
    if (deviceId !== STM32_DEVICE_ID) throw new Error(`Unexpected STM32 ID 0x${deviceId.toString(16)}.`);
    if (!session.info.commands.includes(0x31) || !session.info.commands.includes(0x11) ||
        (!session.info.commands.includes(0x44) && !session.info.commands.includes(0x43))) {
      throw new Error('ROM bootloader lacks erase, write, or read-back verification support.');
    }
    await applyBinSignalModeOrFallback(session);
    const pageList = fullImage ? Array.from({length: STM32_FLASH_PAGE_COUNT}, (_, i) => i) : pages.map((page) => page.number);
    await session.client.erasePages(pageList, session.info.commands);
    appendFirmwareLog(`${label}: erased ${pageList.length} FLASH page(s).`, 'success');
    for (let i = 0; i < pages.length; ++i) {
      const page = pages[i];
      await session.client.writePage(page.address, page.data);
      const actual = await session.client.readMemory(page.address, page.data.length);
      for (let j = 0; j < actual.length; ++j) {
        if (actual[j] !== page.data[j]) throw new Error(`${label} verification mismatch at 0x${(page.address + j).toString(16)}.`);
      }
      if (i === pages.length - 1 || (i + 1) % Math.max(1, Math.floor(pages.length / 10)) === 0) {
        appendFirmwareLog(`${label}: programmed and verified ${i + 1}/${pages.length} page(s).`);
      }
    }
    programmed = true;
    try {
      await session.client.goToAddress(STM32_FLASH_BASE);
      startedByGo = true;
      appendFirmwareLog(`${label}: ROM GO accepted.`, 'success');
    } catch (error) {
      appendFirmwareLog(`${label}: ROM GO failed (${error.message}); sending firmware-start signal.`, 'warning');
    }
  } finally {
    if (session) await session.client.close(false);
    bootloaderPortOpen = false;
    if (portMayBeOpen) {
      try { await port.close(); portMayBeOpen = false; } catch { portMayBeOpen = true; }
    }
    if (programmed && !startedByGo && !portMayBeOpen) await sendFirmwareStartSignal(port);
  }
}

async function reconnectAndCheckFirmware(port, transport, expectSpi = false) {
  await connectDevice(port, transport);
  if (!serial || !(protocolCapabilities & CAP_GET_STATUS)) throw new Error('Firmware did not answer HELLO after restart.');
  if (expectSpi && (!logInfo?.spiReady || !logInfo?.spiSizeMb)) throw new Error('SPI service started, but no ready SPI NOR chip was detected.');
}

async function restoreSpiRecoveryBackup(port, transport, showStatus = true) {
  const backup = spiRecoveryBackup;
  if (!backup?.bytes || backup.bytes.length !== STM32_FLASH_SIZE) throw new Error('No complete FLASH recovery backup is available.');
  setDumpStatus('Restoring and verifying the original 64 KB FLASH imageвЂ¦', 'warning');
  appendFirmwareLog('Restoring the saved original FLASH image.');
  const pages = validateFirmwareImage(backup.bytes, new Uint8Array(STM32_FLASH_SIZE).fill(1));
  await programFlashImage(port, pages, 'Original firmware restore', true);
  let applicationOnline = true;
  try { await reconnectAndCheckFirmware(port, transport, false); }
  catch (error) {
    applicationOnline = false;
    appendFirmwareLog(`FLASH read-back verification passed, but restored firmware did not answer HELLO: ${error.message}`, 'warning');
  }
  await clearSpiRecoveryBackup();
  if (applicationOnline) {
    appendFirmwareLog('Original firmware restored, read-back verified, and HELLO confirmed.', 'success');
    if (showStatus) setDumpStatus('Original firmware restored and verified. Device is running normally.', 'success');
  } else {
    appendFirmwareLog('FLASH image restored and verified; the saved backup remains available in IndexedDB.', 'success');
    if (showStatus) setDumpStatus('Original 64 KB FLASH was restored and verified, but HELLO was not confirmed. The backup remains available in IndexedDB.', 'warning');
  }
  return applicationOnline;
}

async function runMiniSpiDump(port, transport, startAddress, length) {
  const mini = getEmbeddedSpiMiniImage();
  if (!window.confirm(`The app will stop counting, capture and save a full FLASH backup, temporarily run the SPI service, dump offsets ${startAddress}-${startAddress + length - 1}, then restore and verify the original FLASH. Keep the device connected throughout. Continue?`)) return;
  let backup = null;
  let spiBytes = null;
  let operationError = null;
  let originalApplicationOnline = true;
  try {
    setDumpStatus('Reading and saving the full FLASH recovery backupвЂ¦', 'warning');
    backup = await readFullFlashBackup(port);
    setDumpStatus('Programming temporary SPI service firmwareвЂ¦', 'warning');
    await programFlashImage(port, mini.pages, 'SPI service firmware');
    await reconnectAndCheckFirmware(port, transport, true);
    setDumpStatus('Reading SPI NOR through the temporary service firmwareвЂ¦');
    spiBytes = await readSpiThroughProtocol(serial, length, startAddress);
  } catch (error) {
    operationError = error;
    appendFirmwareLog(`SPI service operation failed: ${error.message}`, 'error');
  }
  if (backup) {
    try { originalApplicationOnline = await restoreSpiRecoveryBackup(port, transport, false); }
    catch (error) {
      appendFirmwareLog(`CRITICAL: automatic FLASH recovery failed: ${error.message}`, 'error');
      setDumpStatus(`CRITICAL: firmware recovery failed. Keep the device connected and use Restore FLASH backup. Download a backup below. ${error.message}`, 'error');
      return;
    }
  }
  if (operationError) {
    setDumpStatus(`SPI dump failed; original firmware was restored and verified. ${operationError.message}`, 'error');
    return;
  }
  const filename = makeDumpFilename(`spi-${startAddress}-${startAddress + length - 1}`);
  downloadDump(spiBytes, filename);
  appendFirmwareLog(`SPI dump saved as ${filename}; original firmware restored.`, 'success');
  setDumpStatus(originalApplicationOnline
    ? `SPI dump saved as ${filename} (${spiBytes.length.toLocaleString()} bytes). Original firmware was restored and verified.`
    : `SPI dump saved as ${filename} (${spiBytes.length.toLocaleString()} bytes). Original FLASH was restored and verified, but HELLO was not confirmed; recovery backup is retained.`
  , originalApplicationOnline ? 'success' : 'warning');
}

async function restoreLatestFlashBackup() {
  if (dumpBusy || firmwareFlashBusy || !latestFlashBackup?.bytes) return;
  if (!window.confirm('Restore the saved 64 KB FLASH image? This erases the current firmware.')) return;
  try {
    const {port, transport} = await selectFirmwarePort();
    setDumpBusy(true);
    setSpiRecoveryBackup(latestFlashBackup.bytes, latestFlashBackup.name);
    await saveLatestFlashBackup(latestFlashBackup.bytes, latestFlashBackup.name, true);
    await restoreSpiRecoveryBackup(port, transport);
  } catch (error) {
    appendFirmwareLog(`FLASH restore failed: ${error.message}`, 'error');
    setDumpStatus(`FLASH restore failed. The saved backup remains available: ${error.message}`, 'error');
  } finally { setDumpBusy(false); }
}

async function dumpSpiMemory(range) {
  if (dumpBusy || firmwareFlashBusy) return;
  if (spiRecoveryBackup) {
    setDumpStatus('A pending FLASH recovery backup must be restored before starting another SPI operation.', 'error');
    return;
  }
  setDumpBusy(true);
  appendFirmwareLog(`SPI dump requested at offsets ${range.start}–${range.end}.`);
  try {
    if (serial && (protocolCapabilities & CAP_GET_SPI_DUMP) !== 0 &&
        logInfo?.spiReady && logInfo.spiSizeMb) {
      try {
        if (range.end >= logInfo.spiSizeMb * 1024 * 1024) throw new Error('Selected range exceeds the detected SPI size.');
        const bytes = await readSpiThroughProtocol(serial, range.length, range.start);
        const filename = makeDumpFilename(`spi-${range.start}-${range.end}`);
        downloadDump(bytes, filename);
        appendFirmwareLog(`Direct SPI dump saved as ${filename}.`, 'success');
        setDumpStatus(`SPI dump saved as ${filename} (${bytes.length.toLocaleString()} bytes).`, 'success');
        return;
      } catch (error) {
        appendFirmwareLog(`Direct SPI read unavailable (${error.message}); trying the recovery-backed service firmware.`, 'warning');
      }
    }
    const {port, transport} = await selectFirmwarePort();
    await runMiniSpiDump(port, transport, range.start, range.length);
  } catch (error) {
    appendFirmwareLog(`SPI dump failed: ${error.message}`, 'error');
    setDumpStatus(`SPI dump failed: ${error.message}`, 'error');
  } finally { setDumpBusy(false); }
}

async function writeSpiImage(imageOverride = null, operationLabel = 'Write', isErase = false) {
  if (dumpBusy || firmwareFlashBusy || (!selectedSpiImage && !imageOverride)) return;
  const image = imageOverride || selectedSpiImage;
  const capacity = SPI_NOR_SIZE;
  let targetStart;
  try { targetStart = getMemoryWriteStart('spi', image.bytes.length); }
  catch (error) {
    elements.spiWriteStatus.textContent = error.message;
    elements.spiWriteStatus.dataset.level = 'error';
    return;
  }
  const targetEnd = targetStart + image.bytes.length;
  let mini;
  try { mini = getEmbeddedSpiMiniImage(); }
  catch (error) {
    elements.spiWriteStatus.textContent = error.message;
    elements.spiWriteStatus.dataset.level = 'error';
    return;
  }
  if (!window.confirm(
    `${operationLabel} ${image.bytes.length.toLocaleString()} SPI NOR bytes from address ${targetStart} through ${targetEnd - 1}? Affected sectors are erased and rewritten; bytes outside the range will be preserved. The app will back up and restore device FLASH. Keep the device connected until the operation finishes.`)) return;

  setDumpBusy(true);
  let port = null;
  let transport = activeTransport;
  let backup = null;
  let operationError = null;
  let firmwareOnline = false;
  try {
    ({port, transport} = await selectFirmwarePort());
    appendFirmwareLog(`SPI NOR ${operationLabel.toLowerCase()} requested at ${targetStart}-${targetEnd - 1} (${image.bytes.length.toLocaleString()} bytes).`);
    elements.spiWriteStatus.textContent = 'Saving a verified FLASH recovery image…';
    elements.spiWriteStatus.dataset.level = 'warning';
    backup = await readFullFlashBackup(port);

    elements.spiWriteStatus.textContent = 'Starting the temporary SPI service…';
    await programFlashImage(port, mini.pages, 'SPI service firmware');
    await reconnectAndCheckFirmware(port, transport, true);
    if (!serial || !logInfo?.spiReady || logInfo.spiSizeMb !== 1) {
      throw new Error('SPI service did not detect a ready 1 MB NOR chip.');
    }

    const firstSector = Math.floor(targetStart / SPI_SECTOR_SIZE) * SPI_SECTOR_SIZE;
    const lastSector = Math.floor((targetEnd - 1) / SPI_SECTOR_SIZE) * SPI_SECTOR_SIZE;
    const sectorCount = (lastSector - firstSector) / SPI_SECTOR_SIZE + 1;
    let sectorsDone = 0;
    for (let sectorStart = firstSector; sectorStart <= lastSector;
         sectorStart += SPI_SECTOR_SIZE) {
      const sectorEnd = Math.min(sectorStart + SPI_SECTOR_SIZE, capacity);
      const sectorLength = sectorEnd - sectorStart;
      const writeStart = Math.max(targetStart, sectorStart);
      const writeEnd = Math.min(targetEnd, sectorEnd);
      const isFullSector = writeStart === sectorStart && writeEnd === sectorEnd;
      const original = isFullSector && !isErase ? null :
        await readSpiThroughProtocol(serial, sectorLength, sectorStart);
      const sectorImage = original ? original.slice() : new Uint8Array(sectorLength);
      sectorImage.set(image.bytes.subarray(writeStart - targetStart,
        writeEnd - targetStart), writeStart - sectorStart);
      const erase = async () => {
        const request = new Uint8Array(4);
        new DataView(request.buffer).setUint32(0, sectorStart, true);
        const response = await serial.request(OP_SPI_ERASE_SECTOR, request, 5000);
        if (response.length !== 1 || response[0] !== 0)
          throw new Error(`SPI sector erase failed at 0x${sectorStart.toString(16)}.`);
      };
      const program = async (data) => {
        for (let offset = 0; offset < data.length; offset += 128) {
          const amount = Math.min(128, data.length - offset);
          const page = data.subarray(offset, offset + amount);
          if (page.every((byte) => byte === 0xff)) continue;
          const request = new Uint8Array(5 + amount);
          new DataView(request.buffer).setUint32(0, sectorStart + offset, true);
          request[4] = amount;
          request.set(page, 5);
          const response = await serial.request(OP_SPI_PROGRAM_PAGE, request, 5000);
          if (response.length !== 1 || response[0] !== 0)
            throw new Error(`SPI write/read-back verification failed at 0x${(sectorStart + offset).toString(16)}.`);
        }
      };
      elements.spiWriteStatus.textContent = `Updating SPI sector ${sectorsDone + 1}/${sectorCount}...`;
      try {
        await erase();
        await program(sectorImage);
        if (isErase) {
          const verified = await readSpiThroughProtocol(serial, sectorLength, sectorStart);
          if (verified.some((byte, index) => byte !== sectorImage[index]))
            throw new Error(`SPI erase verification failed at sector 0x${sectorStart.toString(16)}.`);
        }
      } catch (error) {
        if (original) {
          try {
            await erase();
            await program(original);
          } catch (restoreError) {
            throw new Error(`${error.message}; boundary sector restore failed: ${restoreError.message}`);
          }
        }
        throw error;
      }
      sectorsDone += 1;
      if (sectorsDone % 8 === 0 || sectorsDone === sectorCount) {
        const percent = Math.floor((sectorsDone * 100) / sectorCount);
        elements.spiWriteStatus.textContent = `Updating SPI NOR... ${percent}%`;
        appendFirmwareLog(`SPI sectors programmed and verified ${sectorsDone}/${sectorCount}.`);
      }
    }
    appendFirmwareLog(`SPI ${operationLabel.toLowerCase()} range programmed and verified; surrounding sector bytes preserved.`, 'success');
  } catch (error) {
    operationError = error;
    appendFirmwareLog(`SPI NOR flash failed: ${error.message}`, 'error');
  }

  if (backup) {
    elements.spiWriteStatus.textContent = 'Restoring original device firmware…';
    try { firmwareOnline = await restoreSpiRecoveryBackup(port, transport, false); }
    catch (error) {
      appendFirmwareLog(`CRITICAL: automatic firmware recovery failed: ${error.message}`, 'error');
      elements.spiWriteStatus.textContent = `CRITICAL: device firmware recovery failed. Use Restore FLASH in Firmware; download the recovery backup before disconnecting. ${error.message}`;
      elements.spiWriteStatus.dataset.level = 'error';
      setDumpStatus('Critical recovery failure. The FLASH recovery backup is available in Firmware.', 'error');
      setDumpBusy(false);
      return;
    }
  }
  setDumpBusy(false);
  if (operationError) {
    elements.spiWriteStatus.textContent = backup
      ? `SPI write failed; original firmware was restored. ${operationError.message}`
      : `SPI write failed before a FLASH recovery image was captured. ${operationError.message}`;
    elements.spiWriteStatus.dataset.level = 'error';
  } else if (firmwareOnline) {
    elements.spiWriteStatus.textContent = `SPI range ${targetStart}-${targetEnd - 1} ${isErase ? 'erased' : 'programmed'} and verified; original firmware was restored.`;
    elements.spiWriteStatus.dataset.level = 'success';
  } else {
    elements.spiWriteStatus.textContent = `SPI range ${targetStart}-${targetEnd - 1} ${isErase ? 'erased' : 'programmed'} and verified; original FLASH was restored, but firmware HELLO was not confirmed.`;
    elements.spiWriteStatus.dataset.level = 'warning';
  }
}

async function openBootloaderForImage(mode) {
  const { port } = await selectFirmwarePort();
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
      appendFirmwareLog(`No ROM sync during the initial probe (${error.message}); proceeding with bootloader entry.`);
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
  if (dumpBusy) return;
  if (firmwareFlashBusy) {
    firmwareStopRequested = true;
    elements.flashFirmware.disabled = true;
    elements.flashFirmware.textContent = 'Stopping…';
    appendFirmwareLog('Stop requested; waiting for the current ROM operation to finish.');
    return;
  }
  if (!selectedFirmwareImage) return;
  if (!('serial' in navigator) && !androidUsbAvailable && activeTransport !== 'webusb') {
    appendFirmwareLog('USB serial access is unavailable in this browser.', 'error');
    return;
  }
  firmwareFlashBusy = true;
  updateSpiWriteButton();
  if (elements.quickRestoreFlash) elements.quickRestoreFlash.disabled = true;
  if (elements.firmwareRestoreFlash) elements.firmwareRestoreFlash.disabled = true;
  updateEepromWriteButton();
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
  let startedByRomGo = false;
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
    const port = session?.port || selectedUsbPort || selectedSerialPort;
    if (session) {
      if (success && session.info.commands.includes(0x21) &&
          selectedFirmwareImage.pages.some((page) => page.address === STM32_FLASH_BASE)) {
        try {
          appendFirmwareLog('Starting the application with ROM GO at 0x08000000.');
          await session.client.goToAddress(STM32_FLASH_BASE);
          startedByRomGo = true;
          appendFirmwareLog('ROM GO acknowledged; closing the bootloader session.', 'success');
        } catch (error) {
          appendFirmwareLog(`ROM GO failed: ${error.message}; falling back to the start signal.`, 'warning');
        }
      }
      await session.client.close(false);
      if (startedByRomGo) {
        try { await port.close(); portMayBeOpen = false; }
        catch (error) {
          portMayBeOpen = true;
          appendFirmwareLog(`Could not close the bootloader port after GO: ${error.message}`, 'warning');
        }
      }
      if (success) {
        try {
          elements.flashFirmware.disabled = true;
          elements.flashFirmware.textContent = 'Starting…';
          elements.flashFirmware.classList.remove('button-stop');
          if (imageIdentity === 'ours' || imageIdentity === 'ours-partial') {
            appendFirmwareLog(imageIdentity === 'ours-partial'
              ? 'Starting partially compatible firmware and checking HELLO only…'
              : 'Starting our firmware and checking for HELLO…');
            await startFirmwareAndVerify(port, startedByRomGo);
            startupResult = imageIdentity === 'ours-partial' ? 'partial-app-verified' : 'app-verified';
          } else if (imageIdentity === 'stock') {
            appendFirmwareLog('Starting stock firmware and checking fwid in 115200 8E1…');
            const version = await startStockFirmwareAndVerify(port, startedByRomGo);
            appendFirmwareLog(`Stock firmware answered fwid: ${version}.`, 'success');
            startupResult = 'stock-verified';
          } else {
            const skipReason = imageIdentity === 'ours-incompatible'
              ? 'project image is not on the JS-compatible checksum list'
              : 'image type is unknown';
            appendFirmwareLog(`Skipping the app protocol check because the ${skipReason}; sending the firmware-start signal only.`);
            if (!startedByRomGo) await sendFirmwareStartSignal(port);
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
    updateSpiWriteButton();
    if (elements.quickRestoreFlash) elements.quickRestoreFlash.disabled = dumpBusy || !latestFlashBackup;
    if (elements.firmwareRestoreFlash) elements.firmwareRestoreFlash.disabled = dumpBusy || !latestFlashBackup;
    firmwareStopRequested = false;
    updateEepromWriteButton();
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

async function connectDevice(portOverride = null, transport = null) {
  transport ||= portOverride instanceof NativeBridgePort ? 'native' :
    (portOverride instanceof Cp210xUsbPort ? 'webusb' : 'serial');
  activeTransport = transport;
  protocolCapabilities = 0;
  deviceSettings = null;
  deviceHiddenSettings = null;
  rateHistory.length = 0;
  logInfo = null;
  logRecords = [];
  logViewCache.clear(); logTypeActiveView.clear(); logSelectedDate = ""; logRangeActive = false; logRangeAll = false; logCountLimit = 0; logRangeStart = 0; logRangeEnd = 0; logRangeDefaults.clear(); logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
  logStartOffset = 0;
  logPageTotal = 0;
  updateSpiDumpAvailability();
  setConnection('connecting', 'Connecting…');
  showNotice(transport === 'webusb' ?
    'Select the CP210x USB adapter. DTR/RTS will not be changed.' :
    'Select the device port. The app will wait if opening it restarts the device.', '');
  try {
    serial = new SerialProtocol((frame) => serial?.dispatch(frame), (error) => {
      clearInterval(pollTimer);
      pollTimer = 0;
      serial = null;
      activeTransport = 'serial';
      deviceSettings = null;
      deviceHiddenSettings = null;
      logInfo = null;
      logRecords = [];
      logViewCache.clear(); logTypeActiveView.clear(); logSelectedDate = ""; logRangeActive = false; logRangeAll = false; logCountLimit = 0; logRangeStart = 0; logRangeEnd = 0; logRangeDefaults.clear(); logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
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
    await serial.connect(portOverride, transport);
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
    if (!portMayBeOpen) selectedUsbPort = null;
    activeTransport = 'serial';
    elements.rebootFirmware.disabled = false;
    deviceSettings = null;
    deviceHiddenSettings = null;
    logInfo = null;
    logRecords = [];
    logViewCache.clear(); logTypeActiveView.clear(); logSelectedDate = ""; logRangeActive = false; logRangeAll = false; logCountLimit = 0; logRangeStart = 0; logRangeEnd = 0; logRangeDefaults.clear(); logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
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
  logViewCache.clear(); logTypeActiveView.clear(); logSelectedDate = ""; logRangeActive = false; logRangeAll = false; logCountLimit = 0; logRangeStart = 0; logRangeEnd = 0; logRangeDefaults.clear(); logHasMore = true; logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
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

async function disconnectPortAfterAction(port = selectedSerialPort || selectedUsbPort || selectedNativePort) {
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
  if (!stillOpen) {
    activeTransport = 'serial';
    selectedUsbPort = null;
  }
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
  appendFirmwareLog('Application-start signal: DTR low, RTS high for 1 s, then close.');
  await port.setSignals({ dataTerminalReady: false, requestToSend: true });
  await delay(1000);
  await port.close();
  portMayBeOpen = false;
}

async function tryBootloaderGoToApplication(port) {
  let client = null;
  try {
    if (!portMayBeOpen) {
      await port.open({ baudRate: 115200, dataBits: 8, parity: 'even', stopBits: 1,
        flowControl: 'none', bufferSize: 256 });
      portMayBeOpen = true;
    }
    client = new Stm32Bootloader(port);
    await client.write([0x7f]);
    await client.expectAck('ROM synchronization', 800);
    const info = await client.getCommands();
    if (!info.commands.includes(0x21)) {
      appendFirmwareLog('ROM bootloader does not advertise GO; using the signal-based start.');
      return false;
    }
    const deviceId = await client.getDeviceId();
    if (deviceId !== STM32_DEVICE_ID) throw new Error(`Unexpected STM32 ID 0x${deviceId.toString(16)}.`);
    appendFirmwareLog('ROM bootloader detected; sending GO to 0x08000000.');
    await client.goToAddress(STM32_FLASH_BASE);
    appendFirmwareLog('ROM bootloader accepted GO; application should now be running.', 'success');
    return true;
  } catch (error) {
    appendFirmwareLog(`ROM GO unavailable (${error.message}); trying the signal-based start.`);
    return false;
  } finally {
    if (client) await client.close(false);
    if (portMayBeOpen) {
      try { await port.close(); portMayBeOpen = false; }
      catch { portMayBeOpen = true; }
    }
  }
}

async function startFirmwareAndVerify(port, alreadyStarted = false) {
  if (!alreadyStarted) await sendFirmwareStartSignal(port);
  let probe = null;
  try {
    probe = new SerialProtocol((frame) => probe?.dispatch(frame), () => {});
    await probe.connect(port, port instanceof NativeBridgePort ? 'native' :
      (port instanceof Cp210xUsbPort ? 'webusb' : 'serial'));
    await requestHelloAfterBoot(probe);
  } finally {
    if (probe) await probe.disconnect();
  }
}

async function startStockFirmwareAndVerify(port, alreadyStarted = false) {
  if (!alreadyStarted) await sendFirmwareStartSignal(port);
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
  if (dumpBusy || elements.rebootFirmware.disabled) return;
  elements.rebootFirmware.disabled = true;
  elements.rebootBootloader.disabled = true;
  setRebootStatus('Starting firmware and checking for HELLO…');
  appendFirmwareLog('Starting firmware and checking for HELLO.');
  showNotice('Sending the firmware-start signal and checking that the application responds.', '');
  clearInterval(pollTimer);
  pollTimer = 0;
  setConnection('connecting', 'Starting firmware…');
  let port = null;
  let actionError = null;
  try {
    ({ port } = await selectFirmwarePort());
    if (serial) {
      await serial.disconnect();
      serial = null;
    }
    const startedByRomGo = await tryBootloaderGoToApplication(port);
    await startFirmwareAndVerify(port, startedByRomGo);
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
  if (dumpBusy) return;
  elements.rebootBootloader.disabled = true;
  elements.rebootFirmware.disabled = true;
  let port = null;
  let actionError = null;
  appendFirmwareLog('Starting ROM bootloader entry check.');
  try {
    ({ port } = await selectFirmwarePort());
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
      const message = 'USB port is still open. Press Disconnect in the top-right to retry.';
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
  else if (nativeBridgeAvailable && selectedBridgePort) connectDevice(
    selectedNativePort || new NativeBridgePort(selectedBridgePort), 'native');
  else connectDevice();
});
elements.connectUsb?.addEventListener('click', () => {
  if (!serial && !portMayBeOpen) connectDevice(null, 'webusb');
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
const installButton = document.querySelector('#install-app');
const isIosBrowser = /iphone|ipad|ipod/i.test(navigator.userAgent) ||
  (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
const isStandaloneApp = window.matchMedia('(display-mode: standalone)').matches ||
  navigator.standalone === true;
if (!isStandaloneApp && isIosBrowser) installButton.hidden = false;
window.addEventListener('beforeinstallprompt', (event) => {
  event.preventDefault();
  installPromptEvent = event;
  installButton.hidden = false;
});
installButton.addEventListener('click', async () => {
  if (!installPromptEvent) {
    showNotice('To install: open the browser Share menu and choose “Add to Home Screen”.', '');
    return;
  }
  installPromptEvent.prompt();
  await installPromptEvent.userChoice;
  installPromptEvent = null;
  installButton.hidden = true;
});
window.addEventListener('appinstalled', () => {
  installPromptEvent = null;
  installButton.hidden = true;
  showNotice('App installed.', 'success');
});
if ('serviceWorker' in navigator && window.isSecureContext) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('./service-worker.js').catch(() => {
      /* The online app remains usable if offline caching is unavailable. */
    });
  }, { once: true });
}
renderOfflinePreview();
renderSettingsClock();
window.setInterval(renderSettingsClock, 1000);
async function refreshNativeBridgePorts() {
  if (!elements.bridgePort || !window.location.hostname.match(/^(127\.0\.0\.1|localhost)$/)) return;
  try {
    const response = await fetch('/api/ports', { cache: 'no-store' });
    if (!response.ok) return;
    const ports = await response.json();
    if (!Array.isArray(ports)) return;
    nativeBridgeAvailable = true;
    if (elements.bridgePort.disabled || document.activeElement === elements.bridgePort) return;
    const preferred = (item) => {
      const description = `${item.description || ''} ${item.hwid || ''} ${item.manufacturer || ''}`.toLowerCase();
      if (item.vid === 0x10c4 && item.pid === 0xea60) return 0;
      if (description.includes('cp210') || description.includes('silicon labs')) return 1;
      return 2;
    };
    const ordered = ports.slice().sort((a, b) => preferred(a) - preferred(b) ||
      String(a.port).localeCompare(String(b.port), undefined, { numeric: true }));
    const listKey = ordered.map((item) =>
      [item.port, item.vid, item.pid, item.description, item.hwid].join('|')).join('\\n');
    if (listKey === bridgePortsKey) return;
    bridgePortsKey = listKey;
    elements.bridgePort.replaceChildren();
    for (const item of ordered) {
      const option = document.createElement('option');
      option.value = item.port;
      option.textContent = `${item.port}${item.description ? ` · ${item.description}` : ''}`;
      elements.bridgePort.append(option);
    }
    selectedBridgePort = ordered[0]?.port || '';
    elements.bridgePort.value = selectedBridgePort;
    elements.bridgePort.title = 'Select the Windows COM port';
    if (!selectedNativePort || selectedNativePort.portName !== selectedBridgePort)
      selectedNativePort = selectedBridgePort ? new NativeBridgePort(selectedBridgePort) : null;
    elements.bridgePort.hidden = ports.length === 0;
    elements.connect.title = ports.length ? 'Connect using the Windows local serial bridge' : '';
  } catch { /* The regular Web Serial app remains available without the bridge. */ }
}
elements.bridgePort?.addEventListener('change', () => {
  selectedBridgePort = elements.bridgePort.value;
  selectedNativePort = selectedBridgePort ? new NativeBridgePort(selectedBridgePort) : null;
});
refreshNativeBridgePorts();
window.setInterval(refreshNativeBridgePorts, 3000);
if (webUsbAvailable && elements.connectUsb) elements.connectUsb.hidden = false;
if (!('serial' in navigator)) {
  elements.connect.disabled = true;
  showNotice(androidUsbAvailable ?
    'Connect using USB and select the CP210x adapter.' :
    'Serial access is unavailable in this browser. Use desktop Chrome/Edge or Android Chrome with USB Connect.', 'error');
}
