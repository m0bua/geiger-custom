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
const OP_GET_THEME = 12;
const OP_SET_THEME = 13;
const CAP_GET_STATUS = 1 << 0;
const OP_SET_RTC = 8;
const OP_DEVICE_ACTION = 9;
const ACTION_WAKE_SCREEN = 3;
const ACTION_SET_INDICATION_MODE = 4;
const ACTION_SET_FLASHLIGHT = 5;
const ACTION_SET_SCREEN_ALWAYS_ON = 6;
const CAP_GET_SETTINGS = 1 << 1;
const CAP_SET_SETTING = 1 << 2;
const CAP_GET_SPI_DUMP = 1 << 5;
const CAP_SET_RTC = 1 << 6;
const CAP_PRECISE_RTC = 1 << 10;
const CAP_RATE_CPM_TENTHS = 1 << 11;
const CAP_DEVICE_ACTION = 1 << 7;
const CAP_THEME_CONFIG = 1 << 9;
const THEME_COLORS = [
  {id: 'background', label: 'Background'}, {id: 'text', label: 'Text'},
  {id: 'accent', label: 'Accent'}, {id: 'blue', label: 'Blue'},
  {id: 'green', label: 'Green'}, {id: 'yellow', label: 'Yellow'},
  {id: 'red', label: 'Red'},
];
const THEME_COLOR_GROUPS = [
  {label: 'Interface', start: 0, end: 3},
  {label: 'Rate levels', start: 3, end: 7},
];
const DEFAULT_THEME_RGB565 = [0x0000, 0xd6ba, 0x77e0, 0x001f, 0x07e0, 0xffe0, 0xf800];
const DEFAULT_THEME_DIMMING = 30;
const LIGHT_THEME_RGB565 = [0xffff, 0x0000, 0x0320, 0x02d7, 0x0400, 0xbc21, 0xc145];
const OCEAN_THEME_RGB565 = [0x0005, 0xc7d9, 0x06bf, 0x4e5e, 0x26b3, 0xfe8c, 0xfb4d];
const AMBER_THEME_RGB565 = [0x0000, 0xfef7, 0xfd20, 0x5d7c, 0x8dec, 0xfd80, 0xf2c5];
const VIOLET_THEME_RGB565 = [0x1083, 0xe6de, 0xdbdf, 0x7d1e, 0x8eb3, 0xe60f, 0xe36e];
const MONOKAI_THEME_RGB565 = [0x2144, 0xffde, 0xa705, 0x66dd, 0xa705, 0xe6ce, 0xf92e];
const DRACULA_THEME_RGB565 = [0x2946, 0xffde, 0xbc9f, 0x8f5f, 0x57cf, 0xf7d1, 0xfaaa];
const NORD_THEME_RGB565 = [0x29a8, 0xdefd, 0x8e1a, 0x8518, 0xa5f1, 0xee51, 0xbb0d];
const SOLARIZED_THEME_RGB565 = [0x0146, 0x84b2, 0x245a, 0x245a, 0x84c0, 0xb440, 0xd985];
const THEME_PRESETS = {
  dark: {label: 'Dark', colors: DEFAULT_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
  light: {label: 'Light', colors: LIGHT_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
  ocean: {label: 'Ocean', colors: OCEAN_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
  amber: {label: 'Amber', colors: AMBER_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
  violet: {label: 'Violet', colors: VIOLET_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
  monokai: {label: 'Monokai', colors: MONOKAI_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
  dracula: {label: 'Dracula', colors: DRACULA_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
  nord: {label: 'Nord', colors: NORD_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
  solarized: {label: 'Solarized Dark', colors: SOLARIZED_THEME_RGB565, dimming: DEFAULT_THEME_DIMMING},
};
const MAX_PAYLOAD = 262;
const PROTOCOL_OPCODE_NAMES = new Map([
  [OP_HELLO, 'HELLO'], [OP_GET_STATUS, 'GET_STATUS'],
  [OP_GET_SETTINGS, 'GET_SETTINGS'], [OP_SET_SETTING, 'SET_SETTING'],
  [OP_GET_LOG_INFO, 'GET_LOG_INFO'], [OP_GET_LOG_PAGE, 'GET_LOG_PAGE'],
  [OP_GET_SPI_DUMP_PAGE, 'GET_SPI_DUMP_PAGE'], [OP_SET_RTC, 'SET_RTC'],
  [OP_DEVICE_ACTION, 'DEVICE_ACTION'], [OP_SPI_PROGRAM_PAGE, 'SPI_PROGRAM_PAGE'],
  [OP_SPI_ERASE_SECTOR, 'SPI_ERASE_SECTOR'],
  [OP_GET_THEME, 'GET_THEME'], [OP_SET_THEME, 'SET_THEME'],
]);
const MAX_ENCODED_FRAME = MAX_PAYLOAD + 10 + Math.ceil((MAX_PAYLOAD + 10) / 254);
const LOG_MAX_RECORDS_PER_REQUEST = 10;
const LOG_LOAD_MORE_COUNT = 100;
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
// SHA-256 of the canonical 64 KiB flash image (gaps filled with 0xFF), not the source file.
// Maintain these lists for this JS protocol; clear/review them when the protocol changes.
const KNOWN_STOCK_IMAGE_SHA256S = new Set([
  'a54c7f6c12f21c483e01c0380781b7dd0fc2812f8aaa247bb37ddf3ba3ea8baa', // EN 3.1.18 (Windows)
  '1c70cf47d8a4341fafd9948e86d854616042707817c4d9c3b57bc466e2bf5f2c', // Port 4.1.16
  'cee27644cccea4023118f97d1a990be3bf6acb44748d61c0811f4cee0ed53386', // Port 4.1.17
  'f033d38158dbff97e0533bd2527a658addb7bbb769ca49d1c84e34b774f0c222', // RU 3.0.19 (Windows)
]);
const COMPATIBLE_PROJECT_IMAGE_SHA256 = new Set([
]);
// Known builds with compatible core operations but different settings semantics.
// These images use LED BRIGHT as PWM percentage rather than LED TIME in milliseconds.
const PARTIALLY_COMPATIBLE_PROJECT_IMAGE_SHA256 = new Set([
  'a18769eb3937e0bdd7ca1c3e263f8e2ad3ece6b60795c81ac13c6a6ecd71da20',
  'ca1447612d433d5dfdfcb7d2c7f14d9a4a98fde337fbb9160b896aa1b77fbe81',
]);

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
  rateChartActionHint: document.querySelector('#rate-chart-action-hint'),
  chartRangeMin: document.querySelector('#chart-range-min'),
  chartRangeAvg: document.querySelector('#chart-range-avg'),
  chartRangeMax: document.querySelector('#chart-range-max'),
  chartRangeDose: document.querySelector('#chart-range-dose'),
  chartRangeCnt: document.querySelector('#chart-range-cnt'),
  chartRangeSeconds: document.querySelector('#chart-range-seconds'),
  deviceDate: document.querySelector('#device-date'),
  deviceTime: document.querySelector('#device-time'),
  settingsDeviceDate: document.querySelector('#settings-device-date'),
  settingsDeviceTime: document.querySelector('#settings-device-time'),
  settingsSystemDate: document.querySelector('#settings-system-date'),
  settingsSystemTime: document.querySelector('#settings-system-time'),
  settingsClockDifference: document.querySelector('#settings-clock-difference'),
  settingsClockStatus: document.querySelector('#settings-clock-status'),
  settingsClockHint: document.querySelector('#settings-clock-hint'),
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
  toggleLogsFullscreen: document.querySelector('#toggle-logs-fullscreen'),
  exitLogsFullscreen: document.querySelector('#exit-logs-fullscreen'),
  logTabs: document.querySelector('#log-tabs'),
  logContent: document.querySelector('#log-content'),
  logPagination: document.querySelector('#log-pagination'),
  logPageSize: document.querySelector('#log-page-size'),
  logPageNumberLabel: document.querySelector('#log-page-number-label'),
  logPageNumber: document.querySelector('#log-page-number'),
  logPageStatus: document.querySelector('#log-page-status'),
  logPagePrevious: document.querySelector('#log-page-previous'),
  logPageNext: document.querySelector('#log-page-next'),
  logRangeMode: document.querySelector('#log-range-mode'),
  logRangeExtra: document.querySelector('#log-range-extra'),
  logFillMetric: document.querySelector('#log-fill-metric'),
  exportLogsCsv: document.querySelector('#export-logs-csv'),
  exportAllLogsCsv: document.querySelector('#export-all-logs-csv'),
  importLogs: document.querySelector('#import-logs'),
  importLogsFile: document.querySelector('#import-logs-file'),
  logsDeviceTime: document.querySelector('#logs-device-time'),
  loadLogSelection: document.querySelector('#load-log-selection'),
  logsStatus: document.querySelector('#logs-status'),
  loadMoreLogs: document.querySelector('#load-more-logs'),
  diagnosticsOutput: document.querySelector('#diagnostics-output'),
  diagnosticsPanel: document.querySelector('.diagnostics-panel'),
  copyDiagnostics: document.querySelector('#copy-diagnostics'),
  clearDiagnostics: document.querySelector('#clear-diagnostics'),
  firmwarePage: document.querySelector('#firmware-page'),
  embeddedQuickFlash: document.querySelector('#embedded-quick-flash'),
  stockFirmwareSection: document.querySelector('#stock-firmware-section'),
  stockFirmwareList: document.querySelector('#stock-firmware-list'),
  stockFirmwareSources: document.querySelector('#stock-firmware-sources'),
  firmwareTools: document.querySelector('#firmware-tools'),
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
  firmwareLog: document.querySelector('#firmware-log'),
  dumpFlash: document.querySelector('#dump-flash'),
  restoreManualFlash: document.querySelector('#restore-manual-flash'),
  dumpEeprom: document.querySelector('#dump-eeprom'),
  dumpSpi: document.querySelector('#dump-spi'),
  spiDumpMethod: document.querySelector('#spi-dump-method'),
  spiDownloadsSection: document.querySelector('#spi-downloads-section'),
  spiDownloadsList: document.querySelector('#spi-downloads-list'),
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
  deviceControlsStatus: document.querySelector('#device-controls-status'),
  deviceWakeScreen: document.querySelector('#device-wake-screen'),
  deviceModeButtons: document.querySelectorAll('.device-mode-button'),
  deviceFlashlight: document.querySelector('#device-flashlight'),
  deviceScreenAlwaysOn: document.querySelector('#device-screen-always-on'),
  themeStatus: document.querySelector('#theme-status'),
  themePreset: document.querySelector('#theme-preset'),
  themeColors: document.querySelector('#theme-colors'),
  themeConfigCode: document.querySelector('#theme-config-code'),
  themeApply: document.querySelector('#theme-apply'),
  spiConfirmDialog: document.querySelector('#spi-confirm-dialog'),
  spiConfirmCancel: document.querySelector('#spi-confirm-cancel'),
  spiConfirmAccept: document.querySelector('#spi-confirm-accept'),
  rateFoot: document.querySelector('#rate-foot'),
  rateFootMain: document.querySelector('#rate-foot-main'),
  rateFootCount: document.querySelector('#rate-foot-count'),
  rateExpand: document.querySelector('#rate-expand'),
  rateDetails: document.querySelector('#rate-details'),
  metrics: document.querySelector('.metrics'),
  rateLabel: document.querySelector('#rate-label'),
  rateDose: document.querySelector('#rate-dose'),
  rateWarmup: document.querySelector('#rate-warmup'),
  rateWarmupFill: document.querySelector('#rate-warmup-fill'),
  rateUnit: document.querySelector('#rate-unit'),
  doseLabel: document.querySelector('#dose-label'),
  dose: document.querySelector('#dose'),
  doseUnit: document.querySelector('#dose-unit'),
  doseFoot: document.querySelector('#dose-foot'),
  doseExpand: document.querySelector('#dose-expand'),
  doseDetails: document.querySelector('#dose-details'),
  doseWarmupSpacer: document.querySelector('#dose-warmup-spacer'),
  uptime: document.querySelector('#uptime'),
  debugBatteryPercent: document.querySelector('#debug-battery-percent'),
  debugBatteryDetail: document.querySelector('#debug-battery-detail'),
  debugBatteryStartPercent: document.querySelector('#debug-battery-start-percent'),
  debugBatteryStartDetail: document.querySelector('#debug-battery-start-detail'),
  debugSolarPercent: document.querySelector('#debug-solar-percent'),
  debugSolarRaw: document.querySelector('#debug-solar-raw'),
  debugUsbRaw: document.querySelector('#debug-usb-raw'),
  debugChargePercent: document.querySelector('#debug-charge-percent'),
  debugChargeTime: document.querySelector('#debug-charge-time'),
  debugScreenPercent: document.querySelector('#debug-screen-percent'),
  debugScreenTime: document.querySelector('#debug-screen-time'),
  debugWakePercent: document.querySelector('#debug-wake-percent'),
  debugWakeTime: document.querySelector('#debug-wake-time'),
  debugSleepPercent: document.querySelector('#debug-sleep-percent'),
  debugSleepTime: document.querySelector('#debug-sleep-time'),
  debugCount: document.querySelector('#debug-count'),
  debugDate: document.querySelector('#debug-date'),
  debugTime: document.querySelector('#debug-time'),
  debugModeBadge: document.querySelector('#debug-mode-badge'),
  debugSpi: document.querySelector('#debug-spi'),
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
    logDiagnostic(`Serial connection opening (${transport}).`);
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
    logDiagnostic(`Serial connection open (${transport}).`);
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
      if (!this.closing) {
        const error = failure || new Error('Serial port closed.');
        logDiagnostic('Serial receive loop stopped.', error);
        this.onClose(error);
      }
    }
  }

  consumeByte(byte) {
    if (byte === 0) {
      if (!this.discardFrame && this.frameBytes.length) {
        try { this.onFrame(decodeFrame(Uint8Array.from([...this.frameBytes, 0]))); }
        catch (error) {
          logDiagnostic('RX frame decode failed.', error);
          showNotice(error.message, 'error');
        }
      }
      this.frameBytes = [];
      this.discardFrame = false;
      return;
    }
    if (this.discardFrame) return;
    if (this.frameBytes.length >= MAX_ENCODED_FRAME) {
      this.frameBytes = [];
      this.discardFrame = true;
      return;
    }
    this.frameBytes.push(byte);
  }

  async request(opcode, payload = new Uint8Array(), timeoutMs = null, withTiming = false) {
    if (!this.writer || this.closing) throw new Error('Serial port is not open.');
    const requestTimeout = timeoutMs ??
      (this.port instanceof Cp210xUsbPort ? 3500 : 1800);
    this.requestId = (this.requestId + 1) & 0xffff;
    if (this.requestId === 0) this.requestId = 1;
    const id = this.requestId;
    const startedAt = performance.now();
    const commandName = PROTOCOL_OPCODE_NAMES.get(opcode) || `OP_${opcode}`;
    const promise = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        logDiagnostic(`RX timeout ${commandName} id=${id} after ${(performance.now() - startedAt).toFixed(1)} ms.`);
        reject(new Error('Device did not respond. Check the USB connection and try again.'));
      }, requestTimeout);
      this.pending.set(id, { opcode, resolve, reject, timeout, startedAt, withTiming });
    });
    try {
      logDiagnostic(`TX ${commandName} id=${id} ${formatProtocolPayload(payload)}.`);
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
      logDiagnostic(`TX failed ${commandName} id=${id}.`, error);
      throw error;
    }
    return promise;
  }

  requestTimed(opcode, payload = new Uint8Array(), timeoutMs = null) {
    return this.request(opcode, payload, timeoutMs, true);
  }

  dispatch(frame) {
    const pending = this.pending.get(frame.requestId);
    const commandName = PROTOCOL_OPCODE_NAMES.get(frame.opcode) || `OP_${frame.opcode}`;
    if (!pending || pending.opcode !== frame.opcode) {
      logDiagnostic(`RX unmatched ${commandName} id=${frame.requestId} ${formatProtocolPayload(frame.payload)}.`);
      return;
    }
    clearTimeout(pending.timeout);
    this.pending.delete(frame.requestId);
    logDiagnostic(`RX ${commandName} id=${frame.requestId} ${(performance.now() - pending.startedAt).toFixed(1)} ms ${formatProtocolPayload(frame.payload)}.`);
    const receivedAt = performance.now();
    pending.resolve(pending.withTiming ?
      {payload: frame.payload, startedAt: pending.startedAt, receivedAt} : frame.payload);
  }

  async disconnect() {
    if (this.closing) return;
    logDiagnostic('Serial disconnect requested.');
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
    logDiagnostic(portMayBeOpen ?
      'Serial disconnect finished, but the port may still be open.' :
      'Serial connection closed.');
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
let memoryDownloads = [];
let latestFlashBackup = null;
let manualFlashBackup = null;
let spiMiniImageCache = null;
let selectedEepromImage = null;
let selectedSpiImage = null;
let firmwareFlashBusy = false;
let firmwareBackupBusy = false;
let firmwareStopRequested = false;
let dumpBusy = false;
let pollTimer = 0;
let fastRatePollTimer = 0;
let polling = false;
let protocolCapabilities = 0;
let protocolMaxPayload = 64;
let deviceSettings = null;
let deviceHiddenSettings = null;
let deviceTheme = null;
let deviceThemeDimming = null;
let themeDraft = DEFAULT_THEME_RGB565.slice();
let themeDraftDimming = DEFAULT_THEME_DIMMING;
let themePreset = 'dark';
let demoTheme = DEFAULT_THEME_RGB565.slice();
let demoThemeDimming = DEFAULT_THEME_DIMMING;
let themeImportInvalid = false;
let selectedResetAction = 0;
let logInfo = null;
let selectedLogType = 'minutes';
let logRecords = [];
let logDisplayPage = 0;
let offlineImportedLogStore = null;
let offlineLogsImported = false;
let logStartOffset = 0;
let rateHistoryUnit = 'CPM';
let rateLevel = 'blue';
let logPageTotal = 0;
let logPageLoading = false;
let logStopRequested = false;
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
let logSortKey = 'date';
let logSortDirection = 'desc';
let logRefreshRetryAt = 0;
const diagnosticsLog = [];
let demoLogRenderer = null;
let clockSyncBusy = false;
let clockConnectedAt = null;
const rateHistory = [];
const rateHistoryLevels = [];
const rateHistoryPulses = [];
const rateHistoryTimes = [];
const RATE_CHART_BUFFER_SECONDS = 1000;
let rateChartZoomSeconds = RATE_CHART_BUFFER_SECONDS;
let rateChartScrollSeconds = 0;
let rateChartDragging = false;
let rateChartDragStartX = 0;
let rateChartDragStartScroll = 0;
let rateChartActionHintTimer = 0;
let previousRateCount = null;
let deviceIndicationMode = null;
const demoSettings = Array.from({ length: 31 }, () => 0);
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
  { id: 30, label: 'LED COLOR', options: ['STOCK', 'THEME'] },
  { id: 28, label: 'LED TIME', options: ['AUTO', '1 ms', '3 ms', '5 ms', '10 ms', '25 ms', '50 ms', '80 ms', '100 ms'] },
  { id: 5, label: 'LED BRIGHT', options: ['AUTO', '1%', '3%', '5%', '10%', '25%', '50%', '80%', '100%'] },
  { id: 6, label: 'SCREEN BRIGHT', options: ['AUTO', '1%', '3%', '5%', '10%', '25%', '50%', '80%', '100%'] },
  { id: 7, label: 'SCREEN TIMER', options: ['1 s', '5 s', '10 s', '30 s', '60 s', '90 s'] },
  { id: 20, label: 'TONE', options: ['STOCK', 'CLASSIC'] },
  { id: 21, label: 'LONG PRESS', options: ['300 ms', '500 ms', '700 ms', '900 ms', '1100 ms', '1300 ms'] },
  { id: 29, label: 'BATT FORMAT', options: ['%', 'V', 'RAW'] },
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

function voltageFromPowerAdc(raw) {
  const centivolts = raw < 1209 ? 270 :
    (raw > 2082 ? 430 : 270 + Math.floor(((raw - 1209) * 160) / 873));
  return `${Math.floor(centivolts / 100)}.${String(centivolts % 100).padStart(2, '0')}V`;
}

function hideLeadingZero() {
  const settings = offlinePreview ? demoSettings : deviceSettings;
  return (settings?.[8] ?? 0) === 1;
}

function formatCps(cpm) {
  let text = (cpm / 60).toFixed(2);
  if (hideLeadingZero()) text = text.replace(/^0\./, '.');
  return text;
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
  const searchRate = perHour && deviceIndicationMode === 2;
  const maxDecimals = searchRate ? 6 :
    (perHour ? (symbol === 'R' ? 1 : 3) : 5);
  const precisionDigits = searchRate ? 6 : 5;
  const decimalsAvailable = digitLimit === null ?
    precisionDigits - integerDigits : digitLimit - Math.max(1, integerDigits);
  const digits = value === 0 ? 0 : Math.max(0,
    Math.min(maxDecimals, decimalsAvailable));
  let text = value.toFixed(digits).replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '');
  if (hideLeadingZero()) text = text.replace(/^0\./, '.');
  return { value: text, unit: `${selected.name}${symbol}${perHour ? '/h' : ''}` };
}

function rateChartData() {
  clampRateChartViewport();
  if (offlinePreview) {
    const end = Math.max(2, RATE_CHART_BUFFER_SECONDS - rateChartScrollSeconds);
    const start = Math.max(0, end - rateChartZoomSeconds);
    const unit = demoSettings[9] ?? 0;
    return {values: Array.from({length: end - start}, (_, index) =>
      (start + index) / (RATE_CHART_BUFFER_SECONDS - 1) *
        (unit === 1 ? 1 / 0.00666 : unit === 2 ? 100 : 1)), pulses: [], levels: [],
      startAge: RATE_CHART_BUFFER_SECONDS - start, endAge: RATE_CHART_BUFFER_SECONDS - end};
  }
  const total = rateHistory.length;
  const end = Math.max(0, total - rateChartScrollSeconds);
  const start = Math.max(0, end - rateChartZoomSeconds);
  return {values: rateHistory.slice(start, end), pulses: rateHistoryPulses.slice(start, end),
    levels: rateHistoryLevels.slice(start, end), times: rateHistoryTimes.slice(start, end),
    startAge: total - start, endAge: total - end};
}

function clampRateChartViewport() {
  rateChartZoomSeconds = Math.max(10, Math.min(RATE_CHART_BUFFER_SECONDS, rateChartZoomSeconds));
  const available = Math.max(0, (offlinePreview ? RATE_CHART_BUFFER_SECONDS : rateHistory.length) - rateChartZoomSeconds);
  rateChartScrollSeconds = Math.max(0, Math.min(available, rateChartScrollSeconds));
}

function resetRateChartViewport() {
  rateChartZoomSeconds = RATE_CHART_BUFFER_SECONDS;
  rateChartScrollSeconds = 0;
  hideRateChartTooltip();
  drawRateChart();
}

function positionRateChartActionHint(pointerEvent) {
  const panel = elements.rateChart.parentElement;
  const panelRect = panel.getBoundingClientRect();
  const hintSize = 33;
  const gap = 9;
  let left = pointerEvent.clientX - panelRect.left + gap;
  let top = pointerEvent.clientY - panelRect.top + gap;
  if (left + hintSize > panel.clientWidth - 6) {
    left = pointerEvent.clientX - panelRect.left - hintSize - gap;
  }
  if (top + hintSize > panel.clientHeight - 6) {
    top = pointerEvent.clientY - panelRect.top - hintSize - gap;
  }
  elements.rateChartActionHint.style.left = `${Math.max(6, left)}px`;
  elements.rateChartActionHint.style.top = `${Math.max(6, top)}px`;
}

function showRateChartActionHint(mode, label, pointerEvent) {
  clearTimeout(rateChartActionHintTimer);
  elements.rateChartActionHint.dataset.mode = mode;
  elements.rateChartActionHint.setAttribute('aria-label', label);
  elements.rateChartActionHint.hidden = false;
  positionRateChartActionHint(pointerEvent);
  rateChartActionHintTimer = setTimeout(() => {
    elements.rateChartActionHint.hidden = true;
  }, 600);
}

function updateRateChartSummary(values, pulses, levels) {
  const setRateSummary = (element, value, isWarmup) => {
    const settings = offlinePreview ? demoSettings : deviceSettings;
    const unit = settings?.[9] ?? 0;
    const rateUsvh = unit === 1 ? value * 0.00666 : unit === 2 ? value / 100 : value;
    const level = isWarmup ? 'blue' :
      (rateUsvh > 0.9 ? 'red' : (rateUsvh >= 0.4 ? 'yellow' : 'green'));
    element.classList.remove('level-blue', 'level-green', 'level-yellow', 'level-red');
    element.classList.add(`level-${level}`);
  };
  if (!values.length) {
    elements.chartRangeMin.textContent = '—';
    elements.chartRangeAvg.textContent = '—';
    elements.chartRangeMax.textContent = '—';
    for (const element of [elements.chartRangeMin, elements.chartRangeAvg, elements.chartRangeMax]) {
      element.classList.remove('level-blue', 'level-green', 'level-yellow', 'level-red');
    }
  } else {
    const minimum = Math.min(...values);
    const maximum = Math.max(...values);
    const average = values.reduce((sum, value) => sum + value, 0) / values.length;
    const minIndex = values.indexOf(minimum);
    const maxIndex = values.indexOf(maximum);
    elements.chartRangeMin.textContent = `${formatRateChartValue(minimum)} ${rateHistoryUnit}`;
    elements.chartRangeAvg.textContent = `${formatRateChartValue(average)} ${rateHistoryUnit}`;
    elements.chartRangeMax.textContent = `${formatRateChartValue(maximum)} ${rateHistoryUnit}`;
    setRateSummary(elements.chartRangeMin, minimum, levels[minIndex] === 'blue');
    setRateSummary(elements.chartRangeAvg, average,
      levels.length > 0 && levels.every((level) => level === 'blue'));
    setRateSummary(elements.chartRangeMax, maximum, levels[maxIndex] === 'blue');
  }
  const pulseTotal = pulses.reduce((sum, count) => sum + count, 0);
  elements.chartRangeCnt.textContent = offlinePreview ? '—' : formatGroupedNumber(pulseTotal);
  elements.chartRangeSeconds.textContent = values.length ?
    formatGroupedNumber(values.length) : '—';
  const settings = offlinePreview ? demoSettings : deviceSettings;
  const unit = settings?.[9] ?? 0;
  const dose = unit === 1 ? `${formatGroupedNumber(pulseTotal)} CNT` :
    doseValueText(unit, pulseTotal, settings?.[11] ?? 1);
  elements.chartRangeDose.textContent = dose;
  elements.chartRangeDose.classList.remove('level-white', 'level-yellow', 'level-red');
  elements.chartRangeDose.classList.add(logDoseClass({dose: pulseTotal, covered: values.length}));
}

function rateChartThresholds() {
  const unit = (offlinePreview ? demoSettings : deviceSettings)?.[9] ?? 0;
  const convert = (usvh) => unit === 1 ? usvh / 0.00666 :
    unit === 2 ? usvh * 100 : usvh;
  const settings = offlinePreview ? demoSettings : deviceSettings;
  const customUsvh = [0.3, 0.4, 0.5, 0.7, 0.9, 1, 2, 5, 10][settings?.[16] ?? 0] ?? 0.3;
  const thresholds = [
    {key: 'yellow', label: 'Y', value: convert(0.4), color: '#f0d35e'},
    {key: 'red', label: 'R', value: convert(0.9), color: '#ff6666'},
  ];
  const yellow = thresholds[0];
  const red = thresholds[1];
  if (Math.abs(customUsvh - 0.4) <= 0.0001) {
    yellow.label = 'CY';
    yellow.labelColors = ['#b58cff', yellow.color];
  } else if (Math.abs(customUsvh - 0.9) <= 0.0001) {
    red.label = 'CR';
    red.labelColors = ['#b58cff', red.color];
  } else {
    thresholds.push({key: 'custom', label: 'C', value: convert(customUsvh), color: '#b58cff'});
  }
  return thresholds;
}

function formatRateChartValue(value) {
  const settings = offlinePreview ? demoSettings : deviceSettings;
  if ((settings?.[9] ?? 0) === 1) return formatGroupedNumber(Math.round(value));
  const prefix = settings?.[10] ?? 1;
  if (prefix !== 3) value *= [0.001, 1, 1000][prefix];
  if (Math.abs(value) >= 10) return value.toFixed(0);
  if (Math.abs(value) >= 1) return value.toFixed(1).replace(/\.0$/, '');
  return value.toFixed(3).replace(/0+$/, '').replace(/\.$/, '');
}

function formatRateChartTime(seconds) {
  const date = new Date(Date.UTC(2000, 0, 1) + Math.floor(seconds) * 1000);
  const pad = (value) => String(value).padStart(2, '0');
  return `${pad(date.getUTCHours())}:${pad(date.getUTCMinutes())}:${pad(date.getUTCSeconds())}`;
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
  const left = 48, right = 10, top = 20, bottom = 24;
  const plotWidth = Math.max(1, w - left - right);
  const plotHeight = Math.max(1, h - top - bottom);
  const {values, pulses, levels, startAge, endAge} = rateChartData();
  if (offlinePreview) {
    const unit = demoSettings[9] ?? 0;
    rateHistoryUnit = unit === 1 ? 'CPM' :
      formatRadiation(0.14 * (unit === 2 ? 100 : 1), demoSettings[10] ?? 1,
        unit === 2 ? 'R' : 'Sv', true).unit;
  }
  updateRateChartSummary(values, pulses, levels);
  const thresholds = rateChartThresholds();
  const valuePeak = Math.max(...values, 0.01);
  const demoScaleMax = offlinePreview ? (() => {
    const unit = demoSettings[9] ?? 0;
    return unit === 1 ? 1 / 0.00666 : unit === 2 ? 100 : 1;
  })() : null;
  const peak = demoScaleMax ?? valuePeak * 1.12;
  const xFor = (index) => left + (values.length > 1 ? index / (values.length - 1) : 0) * plotWidth;
  const yFor = (value) => top + plotHeight - (value / peak) * plotHeight;
  context.clearRect(0, 0, w, h);
  context.font = '10px "DM Mono", Consolas, monospace';
  context.fillStyle = '#819096';
  context.fillText(rateHistoryUnit, 4, 11);
  if (offlinePreview) {
    context.textAlign = 'right';
    context.fillStyle = '#9b83da';
    context.fillText('DEMO · ILLUSTRATIVE', w - 4, 11);
    context.textAlign = 'left';
  }

  context.strokeStyle = 'rgba(160,186,194,.14)';
  context.fillStyle = '#819096';
  context.lineWidth = 1;
  context.textAlign = 'right';
  for (let tick = 0; tick <= 4; ++tick) {
    const value = peak * tick / 4;
    const y = yFor(value);
    context.beginPath(); context.moveTo(left, y); context.lineTo(w - right, y); context.stroke();
    context.fillText(formatRateChartValue(value), left - 6, y + 3);
  }
  context.textAlign = 'left';
  context.strokeStyle = 'rgba(160,186,194,.30)';
  context.beginPath(); context.moveTo(left, top); context.lineTo(left, top + plotHeight);
  context.lineTo(w - right, top + plotHeight); context.stroke();
  context.fillText(`-${Math.round(values.length ? startAge : RATE_CHART_BUFFER_SECONDS)}s`, left, h - 5);
  context.textAlign = 'right';
  context.fillText(endAge ? `-${Math.round(endAge)}s` : 'now', w - right, h - 5);
  context.textAlign = 'left';

  for (const threshold of thresholds) {
    if (threshold.value > peak) continue;
    const y = yFor(threshold.value);
    context.save();
    context.setLineDash([5, 4]);
    context.strokeStyle = `${threshold.color}99`;
    context.beginPath(); context.moveTo(left, y); context.lineTo(w - right, y); context.stroke();
    context.restore();
    const labelX = left + 5;
    const labelY = y - 3;
    let textX = labelX;
    for (let index = 0; index < threshold.label.length; ++index) {
      context.fillStyle = threshold.labelColors?.[index] ?? threshold.color;
      const letter = threshold.label[index];
      context.fillText(letter, textX, labelY);
      textX += context.measureText(letter).width;
    }
    context.fillStyle = threshold.color;
    const valueLabel = ` ${formatRateChartValue(threshold.value)}`;
    context.fillText(valueLabel, textX, labelY);
  }
  if (values.length < 2) return;

  const levelColor = '#aebbc0';
  const points = values.map((value, index) => ({x: xFor(index), y: yFor(value)}));
  context.beginPath();
  points.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y));
  context.lineTo(points.at(-1).x, top + plotHeight); context.lineTo(points[0].x, top + plotHeight);
  context.closePath(); context.fillStyle = `${levelColor}18`; context.fill();
  context.beginPath();
  points.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y));
  context.strokeStyle = levelColor; context.lineWidth = 1.8; context.lineJoin = 'round'; context.lineCap = 'round'; context.stroke();
  points.forEach((point, index) => {
    const pulseCount = pulses[index] || 0;
    if (!pulseCount) return;
    context.beginPath();
    context.arc(point.x, point.y, Math.min(6, 3 + Math.log2(pulseCount + 1)), 0, Math.PI * 2);
    const pulseColors = {blue: '#287bff', green: '#00e58b', yellow: '#f0d35e', red: '#ff6666'};
    context.fillStyle = pulseColors[levels[index]] || '#e6edf0';
    context.fill();
    context.lineWidth = 1; context.strokeStyle = '#0b1115'; context.stroke();
  });
  if (!offlinePreview && rateHoverIndex !== null && points[rateHoverIndex]) {
    const point = points[rateHoverIndex];
    context.beginPath(); context.arc(point.x, point.y, 5, 0, Math.PI * 2);
    context.fillStyle = levelColor; context.fill(); context.lineWidth = 2;
    context.strokeStyle = '#e6edf0'; context.stroke();
  }
}

function hideRateChartTooltip() {
  rateHoverIndex = null;
  elements.rateTooltip.hidden = true;
}

function showRateChartTooltip(event) {
  if (rateChartDragging) return;
  const chart = rateChartData();
  if (offlinePreview || !serial || chart.values.length === 0) {
    hideRateChartTooltip();
    return;
  }
  const canvasRect = elements.rateChart.getBoundingClientRect();
  const panel = elements.rateChart.parentElement;
  const panelRect = panel.getBoundingClientRect();
  const plotLeft = 48;
  const plotRight = 10;
  const plotWidth = Math.max(1, canvasRect.width - plotLeft - plotRight);
  const pointerRatio = Math.max(0, Math.min(1,
    (event.clientX - canvasRect.left - plotLeft) / plotWidth));
  rateHoverIndex = Math.round(pointerRatio * (chart.values.length - 1));
  const value = chart.values[rateHoverIndex];
  const unit = deviceSettings?.[9] ?? 0;
  const display = unit === 1 ?
    { value: formatGroupedNumber(Math.round(value)), unit: 'CPM' } :
    formatRadiation(value, deviceSettings?.[10] ?? 1,
      unit === 2 ? 'R' : 'Sv', true);
  elements.rateTooltip.textContent = `${display.value} ${display.unit}\n${formatGroupedNumber(chart.pulses[rateHoverIndex] || 0)} CNT\n${formatRateChartTime(chart.times[rateHoverIndex])}`;
  elements.rateTooltip.hidden = false;
  drawRateChart();

  const cursorX = event.clientX - panelRect.left;
  const cursorY = event.clientY - panelRect.top;
  const tooltipWidth = elements.rateTooltip.offsetWidth;
  const tooltipHeight = elements.rateTooltip.offsetHeight;
  const left = cursorX + 14 + tooltipWidth <= panel.clientWidth - 8 ?
    cursorX + 14 : cursorX - tooltipWidth - 14;
  const top = cursorY + 14 + tooltipHeight <= panel.clientHeight - 8 ?
    cursorY + 14 : cursorY - tooltipHeight - 14;
  elements.rateTooltip.style.left = `${Math.max(8, Math.min(
    panel.clientWidth - tooltipWidth - 8, left))}px`;
  elements.rateTooltip.style.top = `${Math.max(8, Math.min(
    panel.clientHeight - tooltipHeight - 8, top))}px`;
}

function showNotice(message, kind = '') {
  elements.notice.dataset.kind = kind;
  elements.notice.querySelector('p').textContent = message;
}

function logDiagnostic(message, error = null) {
  const stamp = new Date().toISOString().slice(11, 23);
  const detail = error ? ` ${error.name || 'Error'}: ${error.message || error}` : '';
  const line = `[${stamp}] ${message}${detail}`;
  diagnosticsLog.push(line);
  if (diagnosticsLog.length > 1000) diagnosticsLog.shift();
  if (elements.diagnosticsOutput && elements.diagnosticsPanel?.open)
    elements.diagnosticsOutput.textContent = diagnosticsLog.join('\n');
  if (error) console.error(line, error); else console.debug(line);
}

function formatProtocolPayload(payload) {
  const bytes = payload instanceof Uint8Array ? payload : new Uint8Array(payload || []);
  const visible = bytes.subarray(0, 24);
  const hex = Array.from(visible, (byte) => byte.toString(16).padStart(2, '0')).join(' ');
  return `${bytes.length}B${hex ? ` [${hex}${bytes.length > visible.length ? ` … +${bytes.length - visible.length}B` : ''}]` : ''}`;
}

function selectPage(page, updateUrl = true) {
  if (page === 'instructions') page = 'manual';
  const pages = ['overview', 'debug', 'settings', 'logs', 'firmware', 'manual', 'about'];
  if (!pages.includes(page)) return;
  document.body.classList.toggle('logs-page-active', page === 'logs');
  if (page !== 'logs') setLogsFullscreen(false);
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

function setLogsFullscreen(enabled) {
  const active = Boolean(enabled);
  elements.logsPage.classList.toggle('is-fullscreen', active);
  elements.exitLogsFullscreen.hidden = !active;
  elements.toggleLogsFullscreen.setAttribute('aria-pressed', String(active));
  elements.toggleLogsFullscreen.textContent = active ? '↙' : '⛶';
  const label = active ? 'Exit full screen' : 'Expand logs to full screen';
  elements.toggleLogsFullscreen.setAttribute('aria-label', label);
  elements.toggleLogsFullscreen.title = label;
}

function toggleLogsFullscreen() {
  setLogsFullscreen(!elements.logsPage.classList.contains('is-fullscreen'));
}

function renderDebugLogInfo() {
  if (!logInfo) return;
  const spiError = !logInfo.spiSizeMb;
  elements.debugSpi.textContent = spiError ? 'ERR' : `${logInfo.spiSizeMb} MB`;
  elements.debugSpi.classList.toggle('state-error', spiError);
}

function applyLogInfo(payload, sampledAt = performance.now()) {
  if (![30, 35, 36].includes(payload.length) || payload[0] !== 0 || payload[26] !== 2) {
    throw new ProtocolError('Update the device firmware to use the new logs.');
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  logInfo = {
    memory: payload[1], source: payload[27], spi: payload[27] === 1,
    minutes: view.getUint16(2, true), hours: view.getUint16(4, true),
    days: view.getUint16(6, true), alerts: view.getUint16(8, true), seconds: payload[29],
    calendarValid: !!payload[10], calendarSeconds: view.getUint32(11, true),
    calendarAnchorSeconds: view.getUint32(11, true),
    calendarMilliseconds: payload.length >= 36 ? payload[35] * 1000 /
      (payload[21] === 1 ? 256 : 289) : 0,
    calendarAnchorMilliseconds: payload.length >= 36 ? payload[35] * 1000 /
      (payload[21] === 1 ? 256 : 289) : 0,
    calendarAnchorPerformance: sampledAt,
    calendarAnchorUptime: view.getUint32(22, true),
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
  if (store) {
    const description = spiFaultDescription(logInfo);
    store.textContent = logInfo.error ? (description === 'none recorded' ? 'ERR' : `ERR · ${description}`) :
      (logInfo.memory === 2 ? 'OFF' : 'OK');
    store.title = logInfo.error ? `Storage error: ${description === 'none recorded' ? 'writer fault (no detail reported)' : description}` :
      `Storage: ${['EEPROM', 'SPI', 'OFF'][logInfo.memory] || 'unknown'}`;
  }
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
    const requestStartedAt = performance.now();
    const payload = await activeSerial.request(OP_GET_LOG_INFO, undefined, 10000);
    if (serial === activeSerial) {
      if (logInfoPollingFailed) logDiagnostic('GET_LOG_INFO polling resumed.');
      logInfoPollingFailed = false;
      const before = logInfo;
      applyLogInfo(payload, (requestStartedAt + performance.now()) / 2);
      if (before && (before.spiFaultOperation !== logInfo.spiFaultOperation ||
          before.spiFaultStage !== logInfo.spiFaultStage || before.spiFaultAddress !== logInfo.spiFaultAddress) &&
          logInfo.spiFaultOperation) {
        logDiagnostic(`SPI NOR fault captured: ${spiFaultDescription(logInfo)}`);
      }
      if (before && !before.error && logInfo.error) {
        logDiagnostic(`STORE ERR became active; memory=${logMemoryName(logInfo.memory)}, source=${logMemoryName(logInfo.source)}, SPI-ready=${logInfo.spiReady}, JEDEC=${logInfo.jedec || '—'}, detail=${spiFaultDescription(logInfo)}`);
      } else if (before?.error && !logInfo.error) {
        logDiagnostic('STORE ERR cleared by firmware.');
      } else if (before && ['minutes', 'hours', 'days', 'alerts'].some((key) => before[key] !== logInfo[key])) {
        const rawCounts = Array.from(payload.subarray(2, 10), (byte) => byte.toString(16).padStart(2, '0')).join(' ');
        logDiagnostic(`Stored counts changed: MIN ${before.minutes}->${logInfo.minutes}, HOUR ${before.hours}->${logInfo.hours}, DAY ${before.days}->${logInfo.days}, ALERT ${before.alerts}->${logInfo.alerts}; raw=${rawCounts}`);
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
  const operations = ['', 'INIT', 'READ', 'PROGRAM', 'ERASE', 'WRITE'];
  const writerReasons = ['', 'unspecified writer fault', 'minute append failed',
    'alert append failed', 'storage layout/recovery failed',
    'alert expiry failed', 'hour/day rollup append failed'];
  const stages = ['', 'transfer failed', 'corrupt committed header', 'invalid header fields', 'corrupt committed record',
    'WIP timeout', 'WEL not set', 'command transfer failed',
    'verify read failed', 'verify mismatch', 'JEDEC mismatch', 'invalid request/state'];
  const operation = operations[info.spiFaultOperation] || `OP${info.spiFaultOperation}`;
  const reason = info.spiFaultOperation === 5 ? writerReasons[info.spiFaultStage] : stages[info.spiFaultStage];
  const detail = reason || `reason ${info.spiFaultStage}`;
  return info.spiFaultOperation === 2 ? `${operation} / ${detail} @ 0x${info.spiFaultAddress.toString(16).toUpperCase().padStart(6, '0')}` : `${operation} / ${detail}`;
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
  logStartOffset = 0; logPageTotal = 0; logDisplayPage = 0;
}
function isLatestLogView() {
  return elements.logRangeMode.value === 'latest';
}
function isRelativeLogView() {
  return ['hour', 'day', 'month'].includes(elements.logRangeMode.value);
}
function hasCurrentLogDate() {
  const now = logInfo?.calendarValid ? logInfo.calendarSeconds : 0;
  return isLatestLogView() || Boolean(logInfo?.calendarValid && logRangeActive &&
    logRangeStart <= now && logRangeEnd >= now && logRangeEnd >= logRangeStart);
}
function logDateLimit() {
  return logRangeActive && !logRangeAll && !isLatestLogView() ? logRangeEnd : 0;
}
function logRecordInRange(record) {
  return !logRangeActive || logRangeAll || isLatestLogView() || (record.calendar &&
    record.timestamp >= logRangeStart && record.timestamp <= logRangeEnd);
}
function canLoadOlderLogRange() {
  const mode = elements.logRangeMode.value;
  return logRangeActive && !logRangeAll && !isLatestLogView() &&
    ['hour', 'day', 'month', 'dates'].includes(mode) &&
    logRangeStart > 0 && logRangeEnd >= logRangeStart;
}
function canLoadMoreLogRecords() {
  return logHasMore || canLoadOlderLogRange();
}

function calendarSecondsFromInputs(date, time = '00:00') {
  const [year, month, day] = date.split('-').map(Number);
  const [hour, minute] = time.split(':').map(Number);
  return Math.floor(Date.UTC(year, month - 1, day, hour, minute) / 1000) -
    Date.UTC(2000, 0, 1) / 1000;
}

function initializeCalendarRangeInputs(mode) {
  const dateInput = document.querySelector('#log-calendar-date');
  if (!dateInput || dateInput.value) return;
  const fromDevice = Boolean(logInfo?.calendarValid);
  const now = fromDevice ?
    new Date(Date.UTC(2000, 0, 1) + Math.floor(logInfo.calendarSeconds) * 1000) :
    new Date();
  const year = fromDevice ? now.getUTCFullYear() : now.getFullYear();
  const month = fromDevice ? now.getUTCMonth() : now.getMonth();
  const day = fromDevice ? now.getUTCDate() : now.getDate();
  const hour = fromDevice ? now.getUTCHours() : now.getHours();
  if (mode === 'month') {
    dateInput.value = `${year}-${String(month + 1).padStart(2, '0')}`;
  } else {
    dateInput.value = `${year}-${String(month + 1).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
  }
  if (mode === 'hour') {
    const hourInput = document.querySelector('#log-calendar-hour');
    if (hourInput) hourInput.value = String(hour).padStart(2, '0');
  }
}

function shiftCalendarRangeInput(mode, delta) {
  const dateInput = document.querySelector('#log-calendar-date');
  if (!dateInput?.value) return;
  if (mode === 'month') {
    const [year, month] = dateInput.value.split('-').map(Number);
    const date = new Date(Date.UTC(year, month - 1 + delta, 1));
    dateInput.value = `${date.getUTCFullYear()}-${String(date.getUTCMonth() + 1).padStart(2, '0')}`;
    return;
  }
  const hour = mode === 'hour' ? Number(document.querySelector('#log-calendar-hour')?.value || 0) : 0;
  const [year, month, day] = dateInput.value.split('-').map(Number);
  const date = new Date(Date.UTC(year, month - 1, day, hour));
  if (mode === 'hour') date.setUTCHours(date.getUTCHours() + delta);
  else date.setUTCDate(date.getUTCDate() + delta);
  dateInput.value = formatDeviceDate(date);
  if (mode === 'hour') {
    document.querySelector('#log-calendar-hour').value = String(date.getUTCHours()).padStart(2, '0');
  }
}

function updateLogRangeControls() {
  const type = logTypes.find((item) => item.id === selectedLogType);
  if (!type) return;
  const mode = elements.logRangeMode.value;
  const datesMode = mode === 'dates';
  const relativeMode = isRelativeLogView();
  const key = `${type.id}:${mode}`;
  const controlsRebuilt = elements.logRangeExtra.dataset.key !== key;
  if (controlsRebuilt) {
    const oldKey = elements.logRangeExtra.dataset.key;
    if (oldKey) {
      const saved = {};
      elements.logRangeExtra.querySelectorAll('input,select').forEach((control) => {
        saved[control.id] = control.value;
      });
      logRangeInputCache.set(oldKey, saved);
    }
    elements.logRangeExtra.replaceChildren();
    if (relativeMode) {
      const previous = document.createElement('button');
      previous.type = 'button';
      previous.className = 'button button-quiet log-range-step';
      previous.textContent = '←';
      previous.setAttribute('aria-label', `Previous ${mode}`);
      previous.addEventListener('click', () => {
        shiftCalendarRangeInput(mode, -1);
        updateLogRangeControls();
      });
      elements.logRangeExtra.append(previous);
      const dateLabel = document.createElement('label');
      const dateInput = document.createElement('input');
      dateInput.id = 'log-calendar-date';
      dateInput.type = mode === 'month' ? 'month' : 'date';
      dateLabel.htmlFor = dateInput.id;
      dateLabel.textContent = mode === 'month' ? 'Month' : 'Date';
      dateLabel.append(dateInput);
      elements.logRangeExtra.append(dateLabel);
      if (mode === 'hour') {
        const hourLabel = document.createElement('label');
        const hourInput = document.createElement('select');
        hourInput.id = 'log-calendar-hour';
        hourLabel.htmlFor = hourInput.id;
        hourLabel.textContent = 'Hour';
        for (let hour = 0; hour < 24; ++hour) {
          const option = document.createElement('option');
          option.value = String(hour).padStart(2, '0');
          option.textContent = option.value;
          hourInput.append(option);
        }
        hourLabel.append(hourInput);
        elements.logRangeExtra.append(hourLabel);
      }
      const next = document.createElement('button');
      next.type = 'button';
      next.className = 'button button-quiet log-range-step';
      next.textContent = '→';
      next.setAttribute('aria-label', `Next ${mode}`);
      next.addEventListener('click', () => {
        shiftCalendarRangeInput(mode, 1);
        updateLogRangeControls();
      });
      elements.logRangeExtra.append(next);
    } else if (mode === 'count') {
      const label = document.createElement('label');
      label.htmlFor = 'log-custom-count';
      label.textContent = 'Records';
      const input = document.createElement('input');
      input.id = 'log-custom-count';
      input.type = 'number';
      input.min = '1';
      input.max = '65535';
      input.step = '1';
      input.value = '1000';
      input.inputMode = 'numeric';
      label.append(input);
      elements.logRangeExtra.append(label);
    } else if (mode === 'dates' && type.id === 'minutes') {
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
    } else if (mode === 'dates') {
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
    elements.logRangeExtra.dataset.key = key;
    const saved = logRangeInputCache.get(key) || {};
    elements.logRangeExtra.querySelectorAll('input,select').forEach((control) => {
      if (saved[control.id] !== undefined) control.value = saved[control.id];
    });
    if (mode === 'dates') initializeCustomRangeInputs(type);
    if (relativeMode) initializeCalendarRangeInputs(mode);
    elements.logRangeExtra.querySelector('#log-custom-count')?.addEventListener('input', updateLogRangeControls);
    elements.logRangeExtra.querySelectorAll('input,select').forEach((control) => {
      if (control.id !== 'log-custom-count') {
        control.addEventListener('input', updateLogRangeControls);
        control.addEventListener('change', updateLogRangeControls);
      }
    });
  }
  if (datesMode && logInfo?.calendarValid && !logRangeDefaults.has(type.id)) {
    setDefaultLogRange(type);
    logRangeDefaults.add(type.id);
  }
  const countMode = mode === 'count';
  elements.logRangeExtra.hidden = !datesMode && !countMode && !relativeMode;
  if (controlsRebuilt && logRangeActive && !logRangeAll && datesMode) populateRangeInputs(type);
  const enabled = datesMode || countMode || relativeMode;
  const countInput = elements.logRangeExtra.querySelector('#log-custom-count');
  const customCount = countInput ? Number(countInput.value) : NaN;
  const validCount = Number.isInteger(customCount) && customCount >= 1 && customCount <= 65535;
  elements.logRangeMode.disabled = logCsvBusy || logPageLoading;
  elements.logRangeExtra.querySelectorAll('input,select').forEach((control) => {
    control.disabled = !enabled || logCsvBusy || logPageLoading;
  });
  elements.loadLogSelection.textContent = logPageLoading ?
    (logStopRequested ? 'Stopping…' : 'Stop') : 'Load';
  elements.loadLogSelection.classList.toggle('button-stop', logPageLoading);
  elements.loadLogSelection.disabled = logPageLoading ? logStopRequested :
    ((!serial && !offlineLogsImported) || !logInfo || logCsvBusy ||
      (relativeMode && (!logInfo.calendarValid || !relativeLogRange())) ||
      (datesMode && (!logInfo.calendarValid || !readCustomLogRange(type))) ||
      (countMode && !validCount));
  elements.exportLogsCsv.disabled = !serial || !logRecords.length || logCsvBusy || logPageLoading;
  elements.loadMoreLogs.hidden = !serial;
  updateLogPagination();
  updateLogActions();
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

function relativeLogRange() {
  if (!logInfo?.calendarValid || !isRelativeLogView()) return null;
  const epoch = Date.UTC(2000, 0, 1) / 1000;
  const mode = elements.logRangeMode.value;
  const dateValue = document.querySelector('#log-calendar-date')?.value;
  if (!dateValue) return null;
  let start;
  let end;
  if (mode === 'hour') {
    const hour = document.querySelector('#log-calendar-hour')?.value;
    if (!hour) return null;
    const startSeconds = calendarSecondsFromInputs(dateValue, `${hour}:00`);
    return {start: startSeconds, end: startSeconds + 3599};
  }
  if (mode === 'day') {
    start = new Date(`${dateValue}T00:00:00Z`);
    end = new Date(start.getTime());
    end.setUTCDate(end.getUTCDate() + 1);
  } else {
    const [year, month] = dateValue.split('-').map(Number);
    start = new Date(Date.UTC(year, month - 1, 1));
    end = new Date(Date.UTC(year, month, 1));
  }
  return {
    start: Math.max(0, Math.floor(start.getTime() / 1000) - epoch),
    end: Math.max(0, Math.floor(end.getTime() / 1000) - epoch) - 1,
  };
}

function updateRelativeLogRange() {
  const range = relativeLogRange();
  if (!range) return false;
  const changed = !logRangeActive || logRangeStart !== range.start || logRangeEnd !== range.end;
  logRangeActive = true;
  logRangeAll = false;
  logCountLimit = 0;
  logRangeStart = range.start;
  logRangeEnd = range.end;
  logSelectedDate = elements.logRangeMode.value;
  if (changed) {
    logRecords = logRecords.filter(logRecordInRange);
    saveLogView();
  }
  return changed;
}

function logRecordKey(record) {
  return `${record.epoch}:${record.timestamp}:${selectedLogType === 'alerts' ? record.id : 0}`;
}

function logSortValue(record, key, type) {
  if (key === 'date') return record.timestamp;
  if (key === 'duration' || (key === 'coverage' && type.id === 'alerts')) return record.covered;
  if (key === 'dose') return record.dose;
  if (key === 'average') return record.average;
  if (key === 'maximum') return record.maximum;
  if (key === 'coverage') return logCoveragePercent(record, type);
  return 0;
}

function sortedLogRecords(type) {
  const direction = logSortDirection === 'asc' ? 1 : -1;
  return [...logRecords].sort((left, right) => {
    const difference = logSortValue(left, logSortKey, type) - logSortValue(right, logSortKey, type);
    return difference ? difference * direction : right.timestamp - left.timestamp;
  });
}

function updateLogPagination(total = logRecords.length) {
  const enabled = Boolean(logInfo && total > 100);
  elements.logPagination.hidden = !enabled;
  elements.logPageSize.disabled = !enabled || logPageLoading || logCsvBusy;
  if (!enabled) {
    logDisplayPage = 0;
    return;
  }
  const pageSize = elements.logPageSize.value === 'all' ?
    Math.max(1, total) : Number(elements.logPageSize.value) || 1000;
  const pageCount = Math.max(1, Math.ceil(total / pageSize));
  logDisplayPage = Math.min(logDisplayPage, pageCount - 1);
  if (elements.logPageNumber.dataset.pageCount !== String(pageCount)) {
    const options = document.createDocumentFragment();
    for (let page = 0; page < pageCount; ++page) {
      const option = document.createElement('option');
      option.value = String(page);
      option.textContent = formatGroupedNumber(page + 1);
      options.append(option);
    }
    elements.logPageNumber.replaceChildren(options);
    elements.logPageNumber.dataset.pageCount = String(pageCount);
  }
  elements.logPageNumber.value = String(logDisplayPage);
  elements.logPageNumberLabel.hidden = pageCount <= 1;
  elements.logPageNumber.disabled = logCsvBusy || logPageLoading || pageCount <= 1;
  const start = total ? logDisplayPage * pageSize + 1 : 0;
  const end = total ? Math.min(total, (logDisplayPage + 1) * pageSize) : 0;
  elements.logPageStatus.textContent = total ?
    `${formatGroupedNumber(start)}–${formatGroupedNumber(end)} of ${formatGroupedNumber(total)} · Page ${formatGroupedNumber(logDisplayPage + 1)} of ${formatGroupedNumber(pageCount)}` :
    '0 records';
  elements.logPagePrevious.disabled = logDisplayPage === 0 || logCsvBusy || logPageLoading;
  elements.logPageNext.disabled = logDisplayPage + 1 >= pageCount || logCsvBusy || logPageLoading;
}

function visibleLogRecords(sorted) {
  if (sorted.length <= 100) return sorted;
  if (elements.logPageSize.value === 'all') return sorted;
  const pageSize = Number(elements.logPageSize.value) || 1000;
  const start = logDisplayPage * pageSize;
  return sorted.slice(start, start + pageSize);
}

function makeLogHeaderButton(label, key) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'log-sort-button';
  button.dataset.sortKey = key;
  button.textContent = label;
  const active = logSortKey === key;
  button.setAttribute('aria-sort', active ? (logSortDirection === 'asc' ? 'ascending' : 'descending') : 'none');
  button.textContent += active ? (logSortDirection === 'asc' ? ' ↑' : ' ↓') : '';
  button.addEventListener('click', () => {
    if (logSortKey === key) logSortDirection = logSortDirection === 'asc' ? 'desc' : 'asc';
    else { logSortKey = key; logSortDirection = 'desc'; }
    logDisplayPage = 0;
    elements.logContent.scrollTop = 0;
    renderLogSummary();
  });
  return button;
}

function buildLogHeader(type) {
  const header = document.createElement('div');
  header.className = 'log-grid log-grid-header';
  const columns = type.id === 'alerts' ?
    [['Date', 'date'], ['Duration', 'duration'], ['Dose', 'dose'], ['Average', 'average'], ['Maximum', 'maximum']] :
    [['Date', 'date'], ['Dose', 'dose'], ['Average', 'average'], ['Maximum', 'maximum'], ['Coverage', 'coverage']];
  columns.forEach(([label, key]) => header.append(makeLogHeaderButton(label, key)));
  const actionSpace = document.createElement('span');
  actionSpace.className = 'log-row-action';
  header.append(actionSpace);
  return header;
}

async function drillDownLogRecord(record) {
  const sourceType = selectedLogType;
  const targetType = sourceType === 'hours' ? 'minutes' :
    sourceType === 'days' ? 'hours' : sourceType === 'alerts' ? 'minutes' : null;
  if (!targetType || !record.calendar) return;

  const mode = sourceType === 'hours' ? 'hour' :
    sourceType === 'days' ? 'day' : 'dates';
  const period = sourceType === 'hours' ? 3600 :
    sourceType === 'days' ? 86400 : 60;
  const start = Math.floor(record.timestamp / period) * period;
  const end = start + period - 1;

  saveLogView();
  ++logPageGeneration;
  selectedLogType = targetType;
  elements.logRangeMode.value = mode;
  elements.logTabs.querySelectorAll('[data-log-type]').forEach((tab) => {
    const active = tab.dataset.logType === targetType;
    tab.classList.toggle('active', active);
    tab.setAttribute('aria-selected', String(active));
  });
  updateLogRangeControls();

  const epoch = Date.UTC(2000, 0, 1);
  const date = new Date(epoch + start * 1000);
  const dateText = formatDeviceDate(date);
  const hourText = String(date.getUTCHours()).padStart(2, '0');
  if (mode === 'hour') {
    const dateInput = document.querySelector('#log-calendar-date');
    const hourInput = document.querySelector('#log-calendar-hour');
    if (dateInput) dateInput.value = dateText;
    if (hourInput) hourInput.value = hourText;
  } else if (mode === 'day') {
    const dateInput = document.querySelector('#log-calendar-date');
    if (dateInput) dateInput.value = dateText;
  } else {
    const minuteText = String(date.getUTCMinutes()).padStart(2, '0');
    const minuteInput = `${dateText}T${hourText}:${minuteText}`;
    const newer = document.querySelector('#log-newer-minute');
    const older = document.querySelector('#log-older-minute');
    if (newer) newer.value = minuteInput;
    if (older) older.value = minuteInput;
  }

  logRangeActive = true;
  logRangeAll = false;
  logCountLimit = 0;
  logRangeStart = start;
  logRangeEnd = end;
  logSelectedDate = `${start}:${end}`;
  clearLogView();
  elements.logContent.scrollTop = 0;
  renderLogSummary();
  await loadLogPage(false);
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
      if (!serial && offlineLogsImported) {
        selectedLogType = type.id;
        logDisplayPage = 0;
        updateLogRangeControls();
        elements.logTabs.querySelectorAll('[data-log-type]').forEach((tab) => {
          const active = tab.dataset.logType === selectedLogType;
          tab.classList.toggle('active', active);
          tab.setAttribute('aria-selected', String(active));
        });
        loadImportedLogSelection(false);
        return;
      }
      saveLogView(); ++logPageGeneration;
      selectedLogType = type.id;
      logDisplayPage = 0;
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
  const dose = formatLogRadiation(record.dose / 9000);
  const average = formatLogRadiation(record.average * 0.00666, true);
  const maximum = formatLogRadiation(record.maximum * 0.00666, true);
  const values = [cnt ? `${formatGroupedNumber(record.dose)} CNT` : `${dose.value} ${dose.unit}`,
    cnt ? `${record.average} CPM` : `${average.value} ${average.unit}`,
    cnt ? `${record.maximum} CPM` : `${maximum.value} ${maximum.unit}`];
  const duration = formatAlertDuration(record.covered);
  return selectedLogType === 'alerts' ? [duration, ...values] :
    [...values, formatLogCoverage(record)];
}
function formatLogCoverage(record) {
  const type = logTypes.find((item) => item.id === selectedLogType);
  const percent = logCoveragePercent(record, type);
  const state = record.live ? 'live' : (record.partial ? 'partial' : 'full');
  return `${percent}% · ${formatCoverageDuration(record.covered)} · ${state}`;
}
function formatCoverageDuration(seconds) {
  let remaining = Math.max(0, Math.floor(seconds));
  const days = Math.floor(remaining / 86400);
  remaining %= 86400;
  const hours = Math.floor(remaining / 3600);
  remaining %= 3600;
  const minutes = Math.floor(remaining / 60);
  const secs = remaining % 60;
  const mm = String(minutes).padStart(2, '0');
  const ss = String(secs).padStart(2, '0');
  if (days) return `${days}d ${String(hours).padStart(2, '0')}:${mm}:${ss}`;
  if (hours) return `${String(hours).padStart(2, '0')}:${mm}:${ss}`;
  if (minutes) return `${minutes}:${ss}`;
  return `${secs}s`;
}
function logCoveragePercent(record, type = logTypes.find((item) => item.id === selectedLogType)) {
  return Math.round(Math.min(record.covered, type.period) * 100 / type.period);
}
function logCoverageClass(record, type = logTypes.find((item) => item.id === selectedLogType)) {
  const percent = logCoveragePercent(record, type);
  /* Rounded coverage may display 100% even when the stored interval is
   * partial (for example an hour with a few missing seconds).  Green is
   * reserved for a genuinely complete interval. */
  const incomplete = record.partial || record.covered < type.period;
  return incomplete ? (percent >= 90 ? 'coverage-yellow' : 'coverage-red') :
    'coverage-green';
}
function logRateClass(cpm) {
  return cpm * 666 > 90000 ? 'level-red' :
    (cpm * 666 >= 40000 ? 'level-yellow' : 'level-green');
}
function logDoseClass(record) {
  const averageCpm = record.covered ? record.dose * 60 / record.covered :
    (record.dose ? Infinity : 0);
  const rateClass = logRateClass(averageCpm);
  return rateClass === 'level-green' ? 'level-white' : rateClass;
}
function updateLogFillOptions(typeId) {
  if (elements.logFillMetric.dataset.type === typeId) return;
  const options = typeId === 'alerts' ?
    [['duration', 'Duration'], ['dose', 'Dose'], ['maximum', 'Maximum']] :
    [['dose', 'Dose'], ['maximum', 'Maximum'], ['coverage', 'Coverage']];
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
function formatAlertDuration(seconds) {
  const total = Math.max(0, Math.floor(seconds));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor(total / 60) % 60;
  const secs = total % 60;
  if (hours) return `${hours}:${String(minutes).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
  if (minutes) return `${minutes}:${String(secs).padStart(2, '0')}`;
  return String(secs).padStart(2, '0');
}
function renderLogTotal(type) {
  const totalDose = logRecords.reduce((sum, record) => sum + record.dose, 0);
  const covered = logRecords.reduce((sum, record) => sum + record.covered, 0);
  const weightedAverage = covered ? Math.round(logRecords.reduce((sum, record) =>
    sum + record.average * record.covered, 0) / covered) : 0;
  const maximum = logRecords.reduce((max, record) => Math.max(max, record.maximum), 0);
  const isCnt = (deviceSettings?.[9] ?? 0) === 1;
  const dose = isCnt ? `${formatGroupedNumber(totalDose)} CNT` :
    `${formatLogRadiation(totalDose / 9000).value} ${formatLogRadiation(totalDose / 9000).unit}`;
  const average = isCnt ? `${formatGroupedNumber(weightedAverage)} CPM` :
    `${formatLogRadiation(weightedAverage * 0.00666, true).value} ${formatLogRadiation(weightedAverage * 0.00666, true).unit}`;
  const maximumText = isCnt ? `${formatGroupedNumber(maximum)} CPM` :
    `${formatLogRadiation(maximum * 0.00666, true).value} ${formatLogRadiation(maximum * 0.00666, true).unit}`;
  const values = type.id === 'alerts' ?
    [`TOTAL (${logRecords.length})`, formatAlertDuration(covered), dose, average, maximumText] :
    [`TOTAL (${logRecords.length})`, dose, average, maximumText, `${formatCoverageDuration(covered)} total`];
  const row = elements.logContent.querySelector('.log-total') ?? document.createElement('div');
  row.className = 'log-grid log-record log-total';
  while (row.children.length > values.length + 1) row.lastElementChild.remove();
  values.forEach((value, index) => {
    const cell = row.children[index] ?? document.createElement('span');
    let className = '';
    if (index === 2 && type.id === 'alerts') className = `log-metric ${logDoseClass({dose: totalDose, covered})}`;
    else if (index === (type.id === 'alerts' ? 3 : 2)) className = `log-metric ${logRateClass(weightedAverage)}`;
    else if (index === (type.id === 'alerts' ? 4 : 3)) className = `log-metric ${logRateClass(maximum)}`;
    else if (index === 1 && type.id !== 'alerts') className = `log-metric ${logDoseClass({dose: totalDose, covered})}`;
    if (cell.className !== className) cell.className = className;
    if (cell.textContent !== value) cell.textContent = value;
    if (!cell.isConnected) row.append(cell);
  });
  let actionSpace = row.children[values.length];
  if (!actionSpace || !actionSpace.classList.contains('log-row-action')) {
    actionSpace = document.createElement('span');
    actionSpace.className = 'log-row-action';
    row.append(actionSpace);
  }
  if (!row.isConnected) elements.logContent.append(row);
}
function renderLogSummary(preserveRows = false, prependNewRows = true) {
  if (preserveRows && (logSortKey !== 'date' || logSortDirection !== 'desc')) preserveRows = false;
  if (preserveRows && !elements.logContent.querySelector('.log-grid-header')) {
    preserveRows = false;
  }
  if (!preserveRows) elements.logContent.replaceChildren();
  updateLogRangeControls();
  updateLogPagination();
  elements.loadMoreLogs.hidden = !serial;
  elements.loadMoreLogs.disabled = !serial || !logInfo || logPageLoading ||
    !canLoadMoreLogRecords() || (!logRecords.length && !canLoadOlderLogRange());
  if (!logInfo) return;
  const type = logTypes.find((item) => item.id === selectedLogType);
  updateLogFillOptions(type.id);
  elements.logsStatus.textContent = `${formatGroupedNumber(logRecords.length)} records loaded | ${formatGroupedNumber(logInfo[type.countKey])} stored entries${logPageLoading ? (logRangeAll ? ' | loading all logs...' : logRangeActive ? ' | loading range...' : logCountLimit ? ` | loading last ${logCountLimit}...` : '') : ''}${logInfo.memory === 2 ? ' | recording off' : ''}${logInfo.error ? ' | storage error' : ''}`;
  if (!logRecords.length) { elements.logContent.textContent = logPageLoading ? 'Loading records…' : 'No records available.'; return; }
  let header = preserveRows ? elements.logContent.querySelector('.log-grid-header') : null;
  if (!header) {
    header = buildLogHeader(type);
    elements.logContent.append(header);
  }
  renderLogTotal(type);
  const maximum = logRecords.reduce((largest, record) =>
    Math.max(largest, logFillValue(record, type)), 1);
  const existingRows = preserveRows ? new Map(
    [...elements.logContent.querySelectorAll('.log-record:not(.log-total)')]
      .map((row) => [row.dataset.logKey, row])) : null;
  for (const record of visibleLogRecords(sortedLogRecords(type))) {
    const key = logRecordKey(record);
    let row = null;
    if (preserveRows) {
      row = existingRows.get(key) ?? null;
      if (row) existingRows.delete(key);
    }
    if (!row) row = document.createElement('div');
    const isAlert = selectedLogType === 'alerts';
    row.className = 'log-grid log-record';
    row.dataset.logKey = key;
    row.style.setProperty('--bar-width', `${Math.min(100, logFillValue(record, type) * 100 / maximum)}%`);
    row.title = `Fill ${logFillMetric}: ${logFillValue(record, type)} · Clock period ${record.epoch}${record.live ? ' · live' : ''}${record.partial ? ' · incomplete coverage' : ''}`;
    const values = [formatLogDate(record.timestamp, selectedLogType, record.calendar), ...logValueCells(record)];
    const metricClasses = isAlert ? [null, null, logDoseClass(record),
      logRateClass(record.average), logRateClass(record.maximum)] :
      [null, logDoseClass(record), logRateClass(record.average),
        logRateClass(record.maximum), logCoverageClass(record)];
    while (row.children.length > values.length + 1) row.lastElementChild.remove();
    for (let index = 0; index < values.length; ++index) {
      const cell = row.children[index] ?? document.createElement('span');
      const isMetric = isAlert ? index >= 2 && index <= 4 : index >= 1 && index <= 3;
      const className = isMetric ? `log-metric${metricClasses[index] ? ` ${metricClasses[index]}` : ''}` : (metricClasses[index] ?? '');
      if (cell.className !== className) cell.className = className;
      if (cell.textContent !== values[index]) cell.textContent = values[index];
      if (cell.parentElement !== row) row.append(cell);
    }
    const actionIndex = values.length;
    let action = row.children[actionIndex];
    const canDrillDown = ['hours', 'days', 'alerts'].includes(type.id);
    if (canDrillDown) {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'log-row-action log-drilldown-button';
      button.textContent = '↳';
      const targetLabel = type.id === 'hours' ? 'minute records for this hour' :
        type.id === 'days' ? 'hour records for this day' : 'minute record for this alert';
      button.setAttribute('aria-label', `Open ${targetLabel}`);
      button.title = record.calendar ? `Open ${targetLabel}` :
        'Drill-down requires a calendar timestamp.';
      button.disabled = !record.calendar || logPageLoading || logCsvBusy;
      button.addEventListener('click', (event) => {
        event.stopPropagation();
        void drillDownLogRecord(record);
      });
      if (action?.classList.contains('log-row-action')) row.replaceChild(button, action);
      else row.append(button);
    } else {
      if (!action || !action.classList.contains('log-row-action') ||
          action.tagName !== 'SPAN') {
        const spacer = document.createElement('span');
        spacer.className = 'log-row-action';
        if (action) row.replaceChild(spacer, action);
        else row.append(spacer);
      }
    }
    if (!row.isConnected) {
      const firstRecord = elements.logContent.querySelector('.log-record:not(.log-total)');
      if (preserveRows && prependNewRows && firstRecord) elements.logContent.insertBefore(row, firstRecord);
      else elements.logContent.append(row);
    }
  }
  if (preserveRows) {
    for (const row of existingRows.values()) row.remove();
  }
}

async function queryLogPage(type, cursor, limit, count = LOG_MAX_RECORDS_PER_REQUEST) {
  // Read the maximum full records supported by the connected firmware payload.
  count = Math.max(1, Math.min(count, LOG_MAX_RECORDS_PER_REQUEST,
    Math.floor((protocolMaxPayload - 12) / 25)));
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

function loadImportedLogSelection() {
  if (!offlineLogsImported || !logInfo) return;
  const type = logTypes.find((item) => item.id === selectedLogType);
  const mode = elements.logRangeMode.value;
  let range = null;
  if (['hour', 'day', 'month'].includes(mode)) {
    if (!logInfo.calendarValid) {
      elements.logsStatus.textContent = 'Imported records have no calendar timestamps for this range.';
      return;
    }
    range = relativeLogRange();
  } else if (mode === 'dates') {
    if (!logInfo.calendarValid) {
      elements.logsStatus.textContent = 'Imported records have no calendar timestamps for this range.';
      return;
    }
    range = readCustomLogRange(type);
  }
  if (['hour', 'day', 'month', 'dates'].includes(mode) && !range) return;

  if (range) {
    logRangeActive = true;
    logRangeAll = false;
    logCountLimit = 0;
    logRangeStart = range.start;
    logRangeEnd = range.end;
    logSelectedDate = `${range.start}:${range.end}`;
  } else {
    logRangeActive = false;
    logRangeAll = mode === 'all';
    logCountLimit = mode === 'count' ?
      Number(document.querySelector('#log-custom-count')?.value) || 0 : 0;
    logRangeStart = 0;
    logRangeEnd = 0;
    logSelectedDate = mode === 'all' ? 'all' : '';
  }
  const allRecords = offlineImportedLogStore[type.id].slice();
  let records = range ? allRecords.filter(logRecordInRange) :
    allRecords.sort((left, right) => right.timestamp - left.timestamp || right.epoch - left.epoch);
  if (mode === 'count' && logCountLimit) records = records.slice(0, logCountLimit);
  clearLogView();
  logRecords = records;
  renderLogSummary();
  elements.logsStatus.textContent = `Imported archive · ${formatGroupedNumber(logRecords.length)} ${type.label.toLowerCase()} records shown.`;
  updateLogActions();
}

async function loadLogSelection() {
  if (!serial && offlineLogsImported) return loadImportedLogSelection();
  if (!serial || !logInfo) return;
  logRefreshRetryAt = 0;
  const mode = elements.logRangeMode.value;
  if (isRelativeLogView()) {
    if (!logInfo.calendarValid) {
      elements.logsStatus.textContent = 'Set the device clock to use a calendar range.';
      return;
    }
    updateRelativeLogRange();
    saveLogView(); ++logPageGeneration;
    clearLogView();
    renderLogSummary();
    return loadLogPage(false);
  }
  if (mode === 'all') return selectAllLogs();
  const type = logTypes.find((item) => item.id === selectedLogType);
  if (mode === 'count') {
    const count = Number(document.querySelector('#log-custom-count')?.value);
    return loadRecentLogs(count);
  }
  if (mode === 'latest') {
    return loadRecentLogs(LOG_INITIAL_TARGETS[type.id]);
  }
  if (mode !== 'dates') return;
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
  const type = logTypes.find((item) => item.id === selectedLogType);
  if (isLatestLogView()) {
    logCountLimit = logRecords.length + LOG_INITIAL_TARGETS[type.id];
  } else if (logCountLimit) {
    logCountLimit += LOG_LOAD_MORE_COUNT;
  } else if (!logHasMore && canLoadOlderLogRange()) {
    const duration = logRangeEnd - logRangeStart + 1;
    const previousEnd = logRangeStart - 1;
    if (elements.logRangeMode.value === 'month') {
      const epoch = Date.UTC(2000, 0, 1) / 1000;
      const endDate = new Date(Date.UTC(2000, 0, 1) + previousEnd * 1000);
      const monthStart = Date.UTC(endDate.getUTCFullYear(), endDate.getUTCMonth(), 1) / 1000 - epoch;
      logRangeStart = Math.max(0, monthStart);
      logRangeEnd = previousEnd;
    } else {
      logRangeEnd = previousEnd;
      logRangeStart = Math.max(0, logRangeEnd - duration + 1);
    }
    logCursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
    logHasMore = true;
    logSelectedDate = `${logRangeStart}:${logRangeEnd}`;
    if (isRelativeLogView()) {
      const start = new Date(Date.UTC(2000, 0, 1) + logRangeStart * 1000);
      const dateInput = document.querySelector('#log-calendar-date');
      if (dateInput) dateInput.value = elements.logRangeMode.value === 'month' ?
        `${start.getUTCFullYear()}-${String(start.getUTCMonth() + 1).padStart(2, '0')}` :
        formatDeviceDate(start);
      const hourInput = document.querySelector('#log-calendar-hour');
      if (hourInput && elements.logRangeMode.value === 'hour') {
        hourInput.value = String(start.getUTCHours()).padStart(2, '0');
      }
    } else {
      populateRangeInputs(type);
    }
    renderLogSummary();
    saveLogView();
  }
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

function parseCsvRows(text) {
  const rows = [];
  let row = [], field = '', quoted = false;
  const input = text.replace(/^\uFEFF/, '');
  for (let index = 0; index < input.length; ++index) {
    const char = input[index];
    if (quoted) {
      if (char === '"' && input[index + 1] === '"') {
        field += '"';
        ++index;
      } else if (char === '"') quoted = false;
      else field += char;
    } else if (char === '"' && field.length === 0) quoted = true;
    else if (char === ',') { row.push(field); field = ''; }
    else if (char === '\n' || char === '\r') {
      if (char === '\r' && input[index + 1] === '\n') ++index;
      row.push(field); field = '';
      if (row.some((cell) => cell.length)) rows.push(row);
      row = [];
    } else field += char;
  }
  if (quoted) throw new Error('CSV contains an unfinished quoted field.');
  if (field.length || row.length) {
    row.push(field);
    if (row.some((cell) => cell.length)) rows.push(row);
  }
  return rows;
}

function csvTimestampSeconds(text, calendar) {
  if (calendar) {
    const match = /^(\d{2}|\d{4})-(\d{2})-(\d{2})(?:[ T](\d{2}):(\d{2}))?$/.exec(text.trim());
    if (!match) throw new Error(`Invalid calendar timestamp: ${text}`);
    const year = Number(match[1].length === 2 ? `20${match[1]}` : match[1]);
    const month = Number(match[2]), day = Number(match[3]);
    const hour = Number(match[4] || 0), minute = Number(match[5] || 0);
    const date = new Date(Date.UTC(year, month - 1, day, hour, minute));
    if (year < 2000 || year > 2099 || date.getUTCFullYear() !== year ||
        date.getUTCMonth() !== month - 1 || date.getUTCDate() !== day ||
        hour > 23 || minute > 59) throw new Error(`Invalid calendar timestamp: ${text}`);
    return Math.floor(date.getTime() / 1000 - Date.UTC(2000, 0, 1) / 1000);
  }
  const match = /^UP\s+(\d+):(\d{2})(?::(\d{2}))?$/i.exec(text.trim());
  if (!match) throw new Error(`Invalid uptime timestamp: ${text}`);
  const hours = Number(match[1]), minutes = Number(match[2]), seconds = Number(match[3] || 0);
  if (minutes > 59 || seconds > 59) throw new Error(`Invalid uptime timestamp: ${text}`);
  return hours * 3600 + minutes * 60 + seconds;
}

function parseLogCsv(text) {
  const rows = parseCsvRows(text);
  if (rows.length < 2) throw new Error('CSV has no log records.');
  const header = rows[0].map((name) => name.trim().toLowerCase());
  const required = ['timestamp', 'log_type', 'dose', 'dose_unit', 'average',
    'rate_unit', 'maximum', 'covered_seconds', 'status', 'epoch',
    'record_id', 'calendar_valid'];
  const index = Object.fromEntries(required.map((name) => [name, header.indexOf(name)]));
  if (required.some((name) => index[name] < 0)) {
    throw new Error('Unsupported CSV columns. Import a CSV exported by this app.');
  }
  const imported = {minutes: [], hours: [], days: [], alerts: []};
  const numeric = (row, column, label) => {
    if (!row[index[column]]?.trim()) throw new Error(`Missing ${label} in CSV.`);
    const value = Number(row[index[column]]);
    if (!Number.isFinite(value) || value < 0) throw new Error(`Invalid ${label} in CSV.`);
    return value;
  };
  const unitName = (value) => value.trim().replace(/[μµu]/gi, 'µ').replace(/\s/g, '');
  for (let rowIndex = 1; rowIndex < rows.length; ++rowIndex) {
    const row = rows[rowIndex];
    if (row.length !== header.length) throw new Error(`CSV row ${rowIndex + 1} has the wrong number of columns.`);
    const type = row[index.log_type].trim().toLowerCase();
    if (!Object.hasOwn(imported, type)) throw new Error(`Unknown log type on CSV row ${rowIndex + 1}.`);
    const calendarFlag = numeric(row, 'calendar_valid', 'calendar flag');
    if (calendarFlag !== 0 && calendarFlag !== 1) throw new Error(`Invalid calendar flag on CSV row ${rowIndex + 1}.`);
    const calendar = calendarFlag === 1;
    const doseUnit = unitName(row[index.dose_unit]);
    const rateUnit = unitName(row[index.rate_unit]);
    const doseFactor = doseUnit === 'CNT' ? 1 : doseUnit === 'µSv' ? 1 / 0.000111 :
      doseUnit === 'µR' ? 1 / 0.0111 : 0;
    const rateFactor = rateUnit === 'CPM' ? 1 : rateUnit === 'µSv/h' ? 1 / 0.00666 :
      rateUnit === 'µR/h' ? 1 / 0.666 : 0;
    if (!doseFactor || !rateFactor) throw new Error(`Unsupported units on CSV row ${rowIndex + 1}.`);
    const dose = Math.round(numeric(row, 'dose', 'dose') * doseFactor);
    const average = Math.round(numeric(row, 'average', 'average rate') * rateFactor);
    const maximum = Math.round(numeric(row, 'maximum', 'maximum rate') * rateFactor);
    const covered = Math.round(numeric(row, 'covered_seconds', 'coverage'));
    const epoch = Math.round(numeric(row, 'epoch', 'epoch'));
    const id = Math.round(numeric(row, 'record_id', 'record id'));
    if (![dose, average, maximum, covered, epoch, id].every(Number.isSafeInteger) ||
        epoch > 65535 || id > 65535 || covered > 0xffffffff) {
      throw new Error(`Out-of-range value on CSV row ${rowIndex + 1}.`);
    }
    const status = row[index.status].trim().toLowerCase();
    if (!['full', 'partial', 'live'].includes(status)) throw new Error(`Unknown record status on CSV row ${rowIndex + 1}.`);
    imported[type].push({timestamp: csvTimestampSeconds(row[index.timestamp], calendar),
      dose, average, maximum, covered, epoch, id, calendar, available: true,
      partial: status === 'partial', live: status === 'live'});
  }
  return imported;
}

function emptyOfflineLogStore() {
  return {minutes: [], hours: [], days: [], alerts: []};
}

function offlineLogCounts(store = offlineImportedLogStore) {
  return {minutes: store?.minutes.length ?? 0, hours: store?.hours.length ?? 0,
    days: store?.days.length ?? 0, alerts: store?.alerts.length ?? 0};
}

function mergeImportedLogStore(target, incoming) {
  for (const type of logTypes) {
    const combined = new Map();
    for (const record of [...(target[type.id] || []), ...(incoming[type.id] || [])]) {
      combined.set(`${record.timestamp}:${record.epoch}:${record.id}`, record);
    }
    target[type.id] = [...combined.values()].sort((a, b) => b.timestamp - a.timestamp || b.epoch - a.epoch);
  }
}

function logCrc16(bytes, initial = 0xffff) {
  let crc = initial;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; ++bit) {
      crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc;
}

function logCrc8(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) {
      crc = crc & 0x80 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
    }
  }
  return crc;
}

function readLogBits(bytes, bitOffset, bitCount) {
  let value = 0;
  for (let bit = 0; bit < bitCount; ++bit) {
    if (bytes[(bitOffset + bit) >> 3] & (1 << ((bitOffset + bit) & 7))) value += 2 ** bit;
  }
  return value;
}

function decodePackedLogRecord(type, bytes, version = 2) {
  const periods = {minutes: 60, hours: 3600, days: 86400, alerts: 60};
  const layout = {
    minutes: {time: 20, count: 19, alert: false},
    hours: {time: 14, count: 24, alert: false},
    days: {time: 9, count: 29, alert: false},
    alerts: {time: 20, count: 25, alert: true},
  }[type];
  if (!layout) return null;
  const legacy = version === 1 && type !== 'alerts';
  const required = layout.alert ? 11 : legacy ? 8 : 9;
  if (bytes.length !== required) return null;
  let bit = 0;
  const periodIndex = readLogBits(bytes, bit, layout.time); bit += layout.time;
  const count = readLogBits(bytes, bit, layout.count); bit += layout.count;
  const maximum = readLogBits(bytes, bit, 19); bit += 19;
  let duration = 0;
  if (layout.alert) { duration = readLogBits(bytes, bit, 12) + 1; bit += 12; }
  const segment = readLogBits(bytes, bit, 6); bit += 6;
  let covered = periods[type];
  if (layout.alert) covered = duration;
  else if (!legacy) {
    const missingCode = readLogBits(bytes, bit, 8); bit += 8;
    covered = missingCode === 0 ? periods[type] :
      Math.max(1, Math.floor((periods[type] * (256 - missingCode) + 128) / 256));
  }
  const periodIndexLimit = {minutes: 527040, hours: 8784, days: 366, alerts: 527040}[type];
  const countLimit = {minutes: 262144, hours: 262144 * 60,
    days: 262144 * 1440, alerts: Math.floor(262144 * 4096 / 60)}[type];
  if (periodIndex >= periodIndexLimit || count > countLimit || maximum > 262144 ||
      segment >= 64 || duration > 4096) return null;
  for (; bit < bytes.length * 8; ++bit) {
    if (bytes[bit >> 3] & (1 << (bit & 7))) return null;
  }
  const average = covered ? Math.floor((count * 60 + Math.floor(covered / 2)) / covered) : 0;
  return {periodIndex, dose: count, average, maximum, covered, segment};
}

function logTimestampFromIndex(type, periodIndex, yearDescriptor, referenceYear) {
  const period = type === 'minutes' || type === 'alerts' ? 60 : type === 'hours' ? 3600 : 86400;
  if (!(yearDescriptor & 0x20)) {
    return {timestamp: (yearDescriptor & 0x1f) * 366 * 86400 + periodIndex * period,
      calendar: false};
  }
  const yearCode = yearDescriptor & 0x1f;
  const delta = ((yearCode - (referenceYear & 0x1f) + 48) % 32) - 16;
  const year = referenceYear + delta;
  if (year < 2000 || year > 2099) return null;
  const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
  const maximumIndex = (leap ? 366 : 365) * 86400 / period;
  if (periodIndex >= maximumIndex) return null;
  const start = Date.UTC(year, 0, 1) / 1000 - Date.UTC(2000, 0, 1) / 1000;
  return {timestamp: start + periodIndex * period, calendar: true};
}

function decodeEepromState(bytes, base, capacities) {
  const state = bytes.subarray(base, base + 15);
  if (state.length !== 15 || state[0] !== 0x4c || state[1] !== 3 ||
      state[14] !== 0xa5 || logCrc8(state.subarray(0, 13)) !== state[13]) return null;
  const heads = [], counts = [];
  for (let index = 0; index < 8; ++index) {
    const packed = readLogBits(state, 24 + index * 7, 7);
    const typeIndex = Math.floor(index / 2);
    if ((index & 1) === 0) {
      if (packed >= capacities[typeIndex]) return null;
      heads.push(packed);
    } else {
      if (packed > capacities[typeIndex]) return null;
      counts.push(packed);
    }
  }
  return {generation: state[2], heads, counts,
    activeEpoch: state[10] | (state[11] << 8)};
}

function parseEepromLogDump(bytes) {
  if (bytes.length !== 2048) throw new Error('EEPROM import requires a complete 2 KiB image.');
  const capacities = [62, 25, 50, 30];
  const stateA = decodeEepromState(bytes, 1998, capacities);
  const stateB = decodeEepromState(bytes, 2013, capacities);
  const state = stateA && stateB ?
    ((((stateB.generation - stateA.generation) << 24) >> 24) > 0 ? stateB : stateA) : stateA || stateB;
  if (!state) throw new Error('This EEPROM image has no supported log index (current format required).');
  const segmentYears = bytes.subarray(1934, 1998);
  const tableCrc = bytes[2044] | (bytes[2045] << 8);
  if (logCrc16(segmentYears) !== tableCrc) throw new Error('EEPROM calendar metadata failed its checksum.');
  const records = emptyOfflineLogStore();
  const bases = [348, 906, 1131, 1581];
  const sizes = [9, 9, 9, 11];
  const types = ['minutes', 'hours', 'days', 'alerts'];
  const referenceYear = new Date().getFullYear();
  for (let typeIndex = 0; typeIndex < types.length; ++typeIndex) {
    const type = types[typeIndex], capacity = capacities[typeIndex];
    for (let age = 0; age < state.counts[typeIndex]; ++age) {
      const slot = (state.heads[typeIndex] + capacity - 1 - age) % capacity;
      const offset = bases[typeIndex] + slot * sizes[typeIndex];
      const decoded = decodePackedLogRecord(type, bytes.subarray(offset, offset + sizes[typeIndex]));
      if (!decoded) throw new Error(`EEPROM ${type} record ${slot} is invalid.`);
      if (segmentYears[decoded.segment] === 0xff) throw new Error(`EEPROM ${type} record has no calendar segment.`);
      const time = logTimestampFromIndex(type, decoded.periodIndex,
        segmentYears[decoded.segment], referenceYear);
      if (!time) throw new Error(`EEPROM ${type} record timestamp is out of range.`);
      const period = type === 'minutes' || type === 'alerts' ? 60 : type === 'hours' ? 3600 : 86400;
      records[type].push({timestamp: time.timestamp, calendar: time.calendar,
        epoch: (state.activeEpoch - ((state.activeEpoch - decoded.segment) & 63)) & 0xffff,
        id: slot, dose: decoded.dose, average: decoded.average, maximum: decoded.maximum,
        covered: decoded.covered, available: true, partial: type !== 'alerts' && decoded.covered < period,
        live: false});
    }
  }
  return {source: 'EEPROM', records};
}

function parseSpiLogDump(bytes) {
  const sectorSize = 4096, headerSize = 32;
  if (bytes.length !== 1024 * 1024) {
    throw new Error('SPI log import requires the complete 1 MiB chip image; partial ranges are unsupported.');
  }
  const sectorCount = bytes.length / sectorSize;
  const partitions = [Math.floor(sectorCount * 90 / 256),
    Math.floor(sectorCount * 30 / 256), Math.floor(sectorCount * 50 / 256)];
  const starts = [0, partitions[0], partitions[0] + partitions[1],
    partitions[0] + partitions[1] + partitions[2]];
  const ends = [starts[1], starts[2], starts[3], sectorCount];
  const streamTypes = ['minutes', 'hours', 'days', 'alerts'];
  const records = emptyOfflineLogStore();
  const referenceYear = new Date().getFullYear();
  let recognizedHeaders = 0, nextId = [0, 0, 0, 0];
  for (let sector = 0; sector < sectorCount; ++sector) {
    const streamIndex = starts.findIndex((start, index) => sector >= start && sector < ends[index]);
    const type = streamTypes[streamIndex];
    const address = sector * sectorSize;
    const header = bytes.subarray(address, address + headerSize);
    if (header.every((byte) => byte === 0xff) || header[17] === 0) continue;
    if (header[0] !== 0x4c || header[1] !== 0x47 || header[2] !== 0x32 ||
        header[3] !== 0x53 || (header[4] !== 1 && header[4] !== 2) ||
        header[16] !== 0xa5 || logCrc16(header.subarray(0, 14)) !==
          (header[14] | (header[15] << 8))) continue;
    const headerType = streamTypes[header[5]];
    if (headerType !== type || header[6] >= 64 || (header[7] & 0xc0)) continue;
    ++recognizedHeaders;
    const version = header[4];
    const dataSize = type === 'alerts' ? 11 : version === 1 ? 8 : 9;
    const slotSize = dataSize + 3;
    const capacity = Math.floor((sectorSize - headerSize) / slotSize);
    const generation = new DataView(bytes.buffer, bytes.byteOffset + address + 8, 4).getUint32(0, true);
    const epoch = header[12] | (header[13] << 8);
    for (let slot = 0; slot < capacity; ++slot) {
      const offset = address + headerSize + slot * slotSize;
      const entry = bytes.subarray(offset, offset + slotSize);
      if (entry.every((byte) => byte === 0xff) || entry[dataSize + 2] !== 0xa5) continue;
      const context = new Uint8Array(11);
      context[0] = header[5]; context[1] = header[6]; context[2] = header[7];
      context[3] = epoch & 0xff; context[4] = epoch >> 8;
      new DataView(context.buffer).setUint32(5, generation, true);
      new DataView(context.buffer).setUint16(9, slot, true);
      const expectedCrc = entry[dataSize] | (entry[dataSize + 1] << 8);
      const actualCrc = logCrc16(entry.subarray(0, dataSize), logCrc16(context));
      if (actualCrc !== expectedCrc) continue;
      const decoded = decodePackedLogRecord(type, entry.subarray(0, dataSize), version);
      if (!decoded || decoded.segment !== header[6]) continue;
      const time = logTimestampFromIndex(type, decoded.periodIndex, header[7], referenceYear);
      if (!time) continue;
      const period = type === 'minutes' || type === 'alerts' ? 60 : type === 'hours' ? 3600 : 86400;
      records[type].push({timestamp: time.timestamp, calendar: time.calendar, epoch,
        id: nextId[streamIndex]++ & 0xffff, dose: decoded.dose, average: decoded.average,
        maximum: decoded.maximum, covered: decoded.covered, available: true,
        partial: type !== 'alerts' && decoded.covered < period, live: false});
    }
  }
  const recordCount = Object.values(records).reduce((sum, list) => sum + list.length, 0);
  if (!recognizedHeaders || !recordCount) throw new Error('No valid Geiger-2 log sectors were found in this SPI image.');
  for (const list of Object.values(records)) {
    list.sort((a, b) => b.epoch - a.epoch || b.timestamp - a.timestamp);
  }
  return {source: 'SPI', records};
}

function parseLogMemoryDump(bytes) {
  if (bytes.length === 2048) return parseEepromLogDump(bytes);
  return parseSpiLogDump(bytes);
}

function activateOfflineImportedLogs() {
  const counts = offlineLogCounts();
  const all = Object.values(offlineImportedLogStore).flat();
  const calendarRecords = all.filter((record) => record.calendar);
  logInfo = {minutes: counts.minutes, hours: counts.hours, days: counts.days,
    alerts: counts.alerts, calendarValid: calendarRecords.length > 0,
    calendarSeconds: calendarRecords.reduce((latest, record) => Math.max(latest, record.timestamp), 0),
    memory: 0, error: false};
  offlineLogsImported = true;
  logViewCache.clear();
  logTypeActiveView.clear();
  logRangeInputCache.clear();
  logRangeDefaults.clear();
  logCountLimit = 0;
  logRangeActive = false;
  logRangeAll = false;
  logRangeStart = 0;
  logRangeEnd = 0;
  elements.logRangeMode.value = 'latest';
  const firstType = logTypes.find((type) => counts[type.id] > 0);
  if (firstType) selectedLogType = firstType.id;
  logRecords = offlineImportedLogStore[selectedLogType].slice();
  clearLogView();
  logRecords = offlineImportedLogStore[selectedLogType].slice();
  updateLogRangeControls();
  renderLogTabs();
  elements.logsStatus.textContent = `Imported logs · ${Object.values(counts).reduce((sum, count) => sum + count, 0)} records`;
}

async function importLogFile(file) {
  if (!file) return;
  const extension = file.name.split('.').pop().toLowerCase();
  let imported;
  let sourceLabel;
  if (extension === 'csv') {
    imported = parseLogCsv(await file.text());
    if (!offlineImportedLogStore) offlineImportedLogStore = emptyOfflineLogStore();
    mergeImportedLogStore(offlineImportedLogStore, imported);
    sourceLabel = 'CSV merged';
  } else if (extension === 'bin') {
    const bytes = new Uint8Array(await file.arrayBuffer());
    imported = parseLogMemoryDump(bytes);
    offlineImportedLogStore = imported.records;
    sourceLabel = `${imported.source} dump loaded`;
  } else {
    throw new Error('Choose an app-exported CSV or a full EEPROM/SPI log dump (.bin).');
  }
  activateOfflineImportedLogs();
  elements.logsStatus.textContent = `${sourceLabel} · ${Object.values(offlineLogCounts()).reduce((sum, count) => sum + count, 0)} records`;
  updateLogActions();
}

function updateLogActions() {
  const offline = !serial;
  const hasArchive = offline && offlineLogsImported;
  elements.importLogs.hidden = !offline;
  elements.exportLogsCsv.hidden = offline && !hasArchive;
  elements.exportAllLogsCsv.hidden = offline && !hasArchive;
  const typeRecordsLoaded = logRecords.length > 0;
  const anyDeviceLogs = logInfo && logTypes.some((type) => logInfo[type.countKey] > 0);
  const anyImportedLogs = offlineLogsImported &&
    Object.values(offlineLogCounts()).some((count) => count > 0);
  elements.exportLogsCsv.disabled = offline ? !hasArchive || !typeRecordsLoaded :
    !serial || !typeRecordsLoaded || logCsvBusy || logPageLoading;
  elements.exportAllLogsCsv.disabled = offline ? !anyImportedLogs :
    !serial || !anyDeviceLogs || logCsvBusy || logPageLoading;
}

function exportLogsCsv() {
  if ((!serial && !offlineLogsImported) || (serial && !logInfo) || logCsvBusy) return;
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

async function fetchLatestLogRecords(type) {
  const target = LOG_INITIAL_TARGETS[type.id];
  const records = [];
  let cursor = {time: 0xffffffff, epoch: 0xffff, skip: 0};
  let more = true;
  while (more && records.length < target) {
    const page = await queryLogPage(type, cursor, 0, Math.min(target - records.length, LOG_MAX_RECORDS_PER_REQUEST));
    records.push(...page.records);
    cursor = page.cursor;
    more = page.more && page.records.length > 0;
  }
  return {records, cursor, more};
}

async function exportAllLogsCsv() {
  if (logCsvBusy) return;
  logCsvBusy = true;
  updateLogRangeControls();
  elements.logsStatus.textContent = 'Preparing Export All…';
  try {
    let entries = [];
    if (!serial && offlineLogsImported) {
      entries = logTypes.flatMap((type) =>
        offlineImportedLogStore[type.id].map((record) => ({record, type})));
    } else if (serial && logInfo) {
      saveLogView();
      for (const type of logTypes) {
        let records;
        const activeKey = logTypeActiveView.get(type.id);
        const cached = activeKey ? logViewCache.get(activeKey) : null;
        if (type.id === selectedLogType) records = logRecords.slice();
        else records = cached?.records?.slice() || [];
        if (!records.length && logInfo[type.countKey] > 0) {
          elements.logsStatus.textContent = `Loading latest ${type.label.toLowerCase()} for Export All…`;
          const latest = await fetchLatestLogRecords(type);
          records = latest.records;
          const key = `${type.id}:latest`;
          logViewCache.set(key, {records: records.slice(), cursor: latest.cursor,
            more: latest.more, rangeActive: false, rangeAll: false, countLimit: 0,
            rangeStart: 0, rangeEnd: 0, selectedDate: '', mode: 'latest'});
          logTypeActiveView.set(type.id, key);
        }
        entries.push(...records.map((record) => ({record, type})));
      }
    }
    if (!entries.length) throw new Error('No loaded log records to export.');
    entries.sort((a, b) => a.type.protocolId - b.type.protocolId ||
      b.record.timestamp - a.record.timestamp || b.record.epoch - a.record.epoch);
    downloadLogCsv(entries, 'device-logs-all.csv');
    elements.logsStatus.textContent = `Exported ${formatGroupedNumber(entries.length)} records across all log types.`;
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
  const target = isLatestLogView() ? (logCountLimit || LOG_INITIAL_TARGETS[type.id]) :
    logRangeActive ? Number.MAX_SAFE_INTEGER : logCountLimit ||
      logRecords.length + LOG_INITIAL_TARGETS[type.id];
  const limit = logDateLimit();
  logDiagnostic(`loadLogPage start type=${type.id} mode=${elements.logRangeMode.value} target=${target} limit=${limit} existing=${logRecords.length}`);
  logStopRequested = false;
  logPageLoading = true; renderLogSummary();
  let loadFailed = false;
  const keys = new Set(logRecords.map(logRecordKey));
  try {
    let crossedRangeStart = false;
    while (generation === logPageGeneration && !logStopRequested &&
        logHasMore && logRecords.length < target) {
      const batchSize = LOG_LOAD_MORE_COUNT;
      const batchEnd = Math.min(target, logRecords.length + batchSize);
      while (generation === logPageGeneration && !logStopRequested &&
          logHasMore && logRecords.length < batchEnd) {
        const page = await queryLogPage(type, logCursor, limit, Math.min(batchSize, batchEnd - logRecords.length));
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
        renderLogSummary(true, false);
        saveLogView();
        await new Promise((resolve) => requestAnimationFrame(resolve));
      }
    }
  } catch (error) {
    loadFailed = true;
    logDiagnostic('loadLogPage failed', error);
    if (generation === logPageGeneration) elements.logsStatus.textContent = error.message;
    if (generation === logPageGeneration && hasCurrentLogDate()) {
      logRefreshRetryAt = Date.now() + 30000;
      elements.logsStatus.textContent = `${error.message} Retrying in 30 s.`;
    }
  } finally {
    const stoppedByUser = logStopRequested;
    logPageLoading = false;
    logStopRequested = false;
    if (stoppedByUser) logPageReloadPending = false;
    updateLogRangeControls();
    if (!loadFailed && generation === logPageGeneration) renderLogSummary();
    if (generation === logPageGeneration) {
      logPageTotal = logRecords.length;
      saveLogView();
      elements.loadMoreLogs.disabled = !serial || !logInfo ||
        !canLoadMoreLogRecords() || (!logRecords.length && !canLoadOlderLogRange());
    }
    if (logPageReloadPending && serial && logInfo) { logPageReloadPending = false; if (!logRecords.length) loadLogPage(false); else if (hasCurrentLogDate()) loadNewerLogRecords(); }
  }
}

async function refreshLatestLogRecord() { return loadNewerLogRecords(false); }
async function loadNewerLogRecords(catchUp = true) {
  if (!serial || !logInfo || logPageLoading || logRefreshBusy || elements.logsPage.hidden || !hasCurrentLogDate()) return;
  if (Date.now() < logRefreshRetryAt) return;
  if (!logRecords.length) return loadLogPage(false);
  logRefreshBusy = true;
  const generation = logPageGeneration, type = logTypes.find((item) => item.id === selectedLogType);
  const known = new Set(logRecords.map(logRecordKey));
  const fresh = []; let cursor = {time: 0xffffffff, epoch: 0xffff, skip: 0}, boundary = false, more = true;
  try {
    while (!boundary && more && generation === logPageGeneration) {
      let batch = 0;
      const batchSize = LOG_LOAD_MORE_COUNT;
      while (batch < batchSize && !boundary && more) {
        const page = await queryLogPage(type, cursor, 0, batchSize - batch);
        if (generation !== logPageGeneration || elements.logsPage.hidden) return;
        for (const record of page.records) { fresh.push(record); if (known.has(logRecordKey(record))) boundary = true; }
        batch += page.records.length; cursor = page.cursor; more = page.more && page.records.length > 0;
        if (!catchUp) boundary = true;
      }
      const replacements = new Map(fresh.map((record) => [logRecordKey(record), record]));
      const older = logRecords.filter((record) => !(type.id === 'alerts' && record.live) && !replacements.has(logRecordKey(record)))
        .map((record) => ({...record, live: false}));
      logRecords = [...replacements.values(), ...older].filter(logRecordInRange);
      renderLogSummary(!catchUp); saveLogView();
    }
  } catch (error) {
    logDiagnostic('loadNewerLogRecords failed', error);
    if (generation === logPageGeneration) {
      logRefreshRetryAt = Date.now() + 30000;
      elements.logsStatus.textContent = `${error.message} Retrying in 30 s.`;
    }
  } finally {
    if (logRefreshRetryAt && Date.now() >= logRefreshRetryAt) logRefreshRetryAt = 0;
    logRefreshBusy = false;
  }
}

async function loadLogInfo() {
  if (!serial || logInfoLoading) return;
  logInfoLoading = true;
  const requestStarted = performance.now();
  try {
    logDiagnostic('GET_LOG_INFO request: read selected storage, physical counts, SPI identification, RTC and writer fault.');
    const requestStartedAt = performance.now();
    const payload = await serial.request(OP_GET_LOG_INFO, undefined, 15000);
    applyLogInfo(payload, (requestStartedAt + performance.now()) / 2);
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
  const activeSerial = serial;
  const resumePolling = !!pollTimer;
  clearInterval(pollTimer);
  pollTimer = 0;
  elements.syncClock.textContent = 'Synchronizing…';
  elements.settingsClockStatus.dataset.level = '';
  elements.settingsClockStatus.textContent = 'Writing system time to the device…';
  updateRtcControls();
  try {
    const idleDeadline = performance.now() + 5000;
    while ((activeSerial.pending.size || activeSerial.activeWrites.size) &&
        performance.now() < idleDeadline) await delay(10);
    if (activeSerial.pending.size || activeSerial.activeWrites.size) {
      throw new Error('Wait for the current device request to finish, then synchronize again.');
    }
    const hasPreciseRtc = (protocolCapabilities & CAP_PRECISE_RTC) !== 0;
    if (hasPreciseRtc) {
      const probe = await activeSerial.requestTimed(OP_GET_STATUS,
        new Uint8Array(), 5000);
      if (probe.payload.length < 25 || probe.payload[0] !== 0) {
        throw new ProtocolError('The device returned an invalid time calibration response.');
      }
      const targetMs = localCalendarMilliseconds(new Date()) +
        (probe.receivedAt - probe.startedAt) / 2;
      let targetSeconds = Math.floor(targetMs / 1000);
      let targetMilliseconds = Math.round(targetMs - targetSeconds * 1000);
      if (targetMilliseconds >= 1000) {
        ++targetSeconds;
        targetMilliseconds = 0;
      }
      const request = new Uint8Array(6);
      const view = new DataView(request.buffer);
      view.setUint32(0, targetSeconds, true);
      view.setUint16(4, targetMilliseconds, true);
      const response = await activeSerial.requestTimed(OP_SET_RTC,
        request, 10000);
      const payload = response.payload;
      if (payload.length !== 6 || payload[0] !== 0) {
        throw new ProtocolError('The device rejected the precise clock update.');
      }
      const receivedView = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
      const deviceMs = receivedView.getUint32(1, true) * 1000 +
        payload[5] * 1000 / (logInfo?.clockSource === 'LSE' ? 256 : 289) +
        (response.receivedAt - response.startedAt) / 2;
      if (logInfo) {
        logInfo.calendarValid = true;
        logInfo.calendarSeconds = Math.floor(deviceMs / 1000);
        logInfo.calendarAnchorSeconds = Math.floor(deviceMs / 1000);
        logInfo.calendarMilliseconds = deviceMs % 1000;
        logInfo.calendarAnchorMilliseconds = deviceMs % 1000;
        logInfo.calendarAnchorPerformance = response.receivedAt;
      }
      updateDeviceDateTime();
    } else {
      const request = new Uint8Array(4);
      const sendTime = new Date();
      const secondsSince2000 = localCalendarSeconds(sendTime);
      new DataView(request.buffer).setUint32(0, secondsSince2000, true);
      const syncStartedAt = Date.now();
      const response = await activeSerial.request(OP_SET_RTC, request, 20000);
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
      } else renderSettingsClock();
    }
    elements.settingsClockStatus.dataset.level = 'success';
    elements.settingsClockStatus.textContent = 'Device clock synchronized to system time.';
  } catch (error) {
    elements.settingsClockStatus.dataset.level = 'error';
    elements.settingsClockStatus.textContent = error.message;
  } finally {
    clockSyncBusy = false;
    if (resumePolling && serial === activeSerial &&
        elements.connection.dataset.state === 'connected') {
      pollTimer = setInterval(() => {
        refreshStatus();
        refreshLatestLogRecord();
        refreshLogCounts();
      }, 1000);
    }
    elements.syncClock.textContent = 'Synchronize';
    updateRtcControls();
  }
}

function setConnection(state, label) {
  const canDisconnect = state === 'connected' || state === 'disconnect-pending';
  elements.connection.dataset.state = state;
  if (state !== 'connected') clockConnectedAt = null;
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
  updateDeviceControls();
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

function localCalendarMilliseconds(date) {
  return localCalendarSeconds(date) * 1000 + date.getMilliseconds();
}

function estimatedDeviceCalendarMilliseconds(now = performance.now()) {
  if (!logInfo?.calendarValid) return null;
  const elapsed = Math.max(0, now - (logInfo.calendarAnchorPerformance ?? now));
  return logInfo.calendarAnchorSeconds * 1000 +
    (logInfo.calendarAnchorMilliseconds ?? 0) + elapsed;
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
  const isConnected = clockConnectedAt !== null &&
    elements.connection.dataset.state === 'connected';
  const withinInitialWindow = isConnected &&
    performance.now() - clockConnectedAt < 30000;
  elements.settingsSystemDate.textContent = formatLocalDate(systemNow);
  elements.settingsSystemTime.textContent = formatLocalTime(systemNow);
  if (!logInfo?.calendarValid) {
    elements.settingsDeviceDate.textContent = 'NOT SET';
    elements.settingsDeviceTime.textContent = '—';
    elements.settingsClockDifference.textContent = '—';
    elements.settingsClockHint.hidden = !withinInitialWindow;
    elements.settingsClockDifference.dataset.state = withinInitialWindow ? 'warming' : 'neutral';
    return;
  }

  const deviceNowMs = estimatedDeviceCalendarMilliseconds();
  const deviceDate = new Date(Date.UTC(2000, 0, 1) + deviceNowMs);
  elements.settingsDeviceDate.textContent = formatDeviceDate(deviceDate);
  elements.settingsDeviceTime.textContent = formatDeviceTime(deviceDate);
  const differenceMs = deviceNowMs - localCalendarMilliseconds(systemNow);
  const warming = isConnected && withinInitialWindow &&
    Math.abs(differenceMs) > 10;
  elements.settingsClockHint.hidden = !warming;
  const sign = differenceMs > 0 ? '+' : (differenceMs < 0 ? '−' : '');
  elements.settingsClockDifference.textContent =
    `${sign}${formatClockDifference(differenceMs)}`;
  elements.settingsClockDifference.dataset.state = warming ? 'warming' :
    (differenceMs > 0 ? 'positive' : (differenceMs < 0 ? 'negative' : 'neutral'));
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
    const date = new Date(Date.UTC(2000, 0, 1) + estimatedDeviceCalendarMilliseconds());
    elements.deviceDate.textContent = [date.getUTCFullYear(),
      String(date.getUTCMonth() + 1).padStart(2, '0'),
      String(date.getUTCDate()).padStart(2, '0')].join('-');
    elements.deviceTime.textContent = formatDeviceTime(date);
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
  clockConnectedAt = performance.now();
  protocolMaxPayload = Math.min(MAX_PAYLOAD, maxPayload);
  /* The current status frame does not carry indication-mode state. Do not
   * leave the offline demo's Normal marker visible after connecting. */
  setDeviceModeActive(null);
  updateRtcControls();
  updateDeviceControls();
  updateSpiDumpAvailability();
  renderSettingsClock();
}

function updateSpiDumpAvailability() {
  if (!elements.dumpSpi) return;
  elements.dumpSpi.disabled = dumpBusy || firmwareFlashBusy || firmwareBackupBusy || !!spiRecoveryBackup;
}

function settingsOptions(setting, currentSettings = deviceSettings) {
  const unit = Number(currentSettings?.[9] ?? 0);
  if (setting.id === 11 && unit === 1) {
    return [{label: 'CNT', value: Number(currentSettings?.[11] ?? 0), disabled: true}];
  }
  if (setting.id === 10 && unit === 1) {
    return ['CPM', 'CPS'];
  }
  if (setting.id === 12) {
    const ratePrefix = Number(currentSettings?.[10] ?? 1);
    const cntMode = unit === 1;
    const mainCountRate = ratePrefix === 1 ? 'CPS' : 'CPM';
    return [
      // Keep the unit choices ordered Sv/h, CPM, CPS, R/h, OFF while
      // preserving the firmware's setting values for each UNIT.
      {label: 'Sv/h', value: cntMode ? 0 : (unit === 0 ? 'duplicate-sv' : 3), disabled: unit === 0},
      {label: 'CPM', value: cntMode ? 4 : 0,
        disabled: cntMode && mainCountRate === 'CPM'},
      {label: 'CPS', value: cntMode ? 3 : 1,
        disabled: cntMode && mainCountRate === 'CPS'},
      {label: 'R/h', value: cntMode ? 1 : (unit === 2 ? 'duplicate-r' : 3), disabled: unit === 2},
      {label: 'OFF', value: 2, disabled: false},
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
  const preview = !serial;
  const currentSettings = deviceSettings ?? demoSettings;
  const settingsProtocolSupported = preview ||
    (protocolCapabilities & (CAP_GET_SETTINGS | CAP_SET_SETTING)) ===
      (CAP_GET_SETTINGS | CAP_SET_SETTING);
  const rows = [];
  for (const setting of remoteSettings) {
    if (setting.id === 19) continue;
    const supportSettings = deviceSettings ?? demoSettings;
    const hasSetting = preview || (deviceSettings !== null &&
      setting.id < deviceSettings.length);
    const actionSupported = !serial ||
      ![14, 18, 19].includes(setting.id) ||
      (protocolCapabilities & CAP_DEVICE_ACTION) !== 0;
    const supported = settingsProtocolSupported && hasSetting && actionSupported;
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
      const rebootSupported = settingsProtocolSupported &&
        deviceSettings !== null && 19 < deviceSettings.length &&
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
          if (setting.id === 9 || setting.id === 10 || setting.id === 23) {
            renderSettings();
            document.querySelector(`#setting-${setting.id}`)?.focus();
          }
          if (setting.id === 9 || setting.id === 10) drawRateChart();
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
    elements.settingsStatus.textContent =
      'Offline preview · temporary changes; reload to restore factory defaults.';
  } else if (deviceSettings === null) {
    const protocolReady =
      (protocolCapabilities & (CAP_GET_SETTINGS | CAP_SET_SETTING)) ===
        (CAP_GET_SETTINGS | CAP_SET_SETTING);
    elements.settingsStatus.textContent = protocolReady ?
      'Waiting for device settings; controls remain disabled.' :
      'Settings unavailable in this firmware; controls disabled.';
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

function rgb565ToHex(color) {
  const red = (color >> 11) & 0x1f;
  const green = (color >> 5) & 0x3f;
  const blue = color & 0x1f;
  const expand = (value, max) => Math.round(value * 255 / max);
  return `#${[expand(red, 31), expand(green, 63), expand(blue, 31)]
    .map((value) => value.toString(16).padStart(2, '0')).join('')}`;
}

function hexToRgb565(hex) {
  const value = Number.parseInt(hex.slice(1), 16);
  return (((value >> 19) & 0x1f) << 11) |
    (((value >> 10) & 0x3f) << 5) | ((value >> 3) & 0x1f);
}

function encodeThemeConfigCode(colors, dimming = DEFAULT_THEME_DIMMING) {
  const bytes = colors.flatMap((color) => [color & 0xff, color >> 8]);
  bytes.push(dimming);
  return `T2:${bytes.map((byte) => byte.toString(16).padStart(2, '0')).join('').toUpperCase()}`;
}

function decodeThemeConfigCode(code) {
  const normalized = code.trim();
  const v2 = /^T2:([0-9a-f]{30})$/i.exec(normalized);
  const v1 = /^T1:([0-9a-f]{28})$/i.exec(normalized);
  const match = v2 || v1;
  if (!match) return null;
  const bytes = Array.from({length: v2 ? 15 : 14}, (_, index) =>
    Number.parseInt(match[1].slice(index * 2, index * 2 + 2), 16));
  const dimming = v2 ? bytes[14] : DEFAULT_THEME_DIMMING;
  if (dimming > 100) return null;
  return {
    colors: THEME_COLORS.map((_, index) =>
      bytes[index * 2] | (bytes[index * 2 + 1] << 8)),
    dimming,
  };
}

function syncThemeConfigCode() {
  elements.themeConfigCode.value = encodeThemeConfigCode(themeDraft, themeDraftDimming);
  elements.themeConfigCode.removeAttribute('aria-invalid');
  themeImportInvalid = false;
}

function detectThemePreset(colors, dimming) {
  for (const [id, preset] of Object.entries(THEME_PRESETS)) {
    if (dimming === preset.dimming &&
        colors.every((color, index) => color === preset.colors[index])) return id;
  }
  return 'custom';
}

function themeCodesValid() {
  return !themeImportInvalid && decodeThemeConfigCode(elements.themeConfigCode.value) !== null &&
    [...elements.themeColors.querySelectorAll('input.theme-color-code')]
    .every((input) => /^#[0-9a-f]{6}$/i.test(input.value));
}

function updateThemeApplyState(supported) {
  const current = serial ? deviceTheme : demoTheme;
  const currentDimming = serial ? deviceThemeDimming : demoThemeDimming;
  elements.themeApply.disabled = !supported || !current || !themeCodesValid() ||
    (themeDraftDimming === currentDimming &&
      themeDraft.every((color, index) => color === current[index]));
}

function renderThemeControls(colors = themeDraft) {
  const supported = !serial || (protocolCapabilities & CAP_THEME_CONFIG) !== 0;
  elements.themePreset.value = themePreset;
  elements.themePreset.disabled = !supported;
  elements.themeColors.replaceChildren();
  THEME_COLOR_GROUPS.forEach(({label: groupLabel, start, end}) => {
    const group = document.createElement('div');
    group.className = 'theme-color-group';
    const title = document.createElement('div');
    title.className = 'theme-color-group-title';
    title.textContent = groupLabel;
    const grid = document.createElement('div');
    grid.className = 'theme-color-grid';
    for (let index = start; index < end; index += 1) {
      const {label} = THEME_COLORS[index];
      const row = document.createElement('div');
      row.className = 'theme-color-control';
      const name = document.createElement('span');
      name.textContent = label;
      const input = document.createElement('input');
      input.type = 'color';
      input.value = rgb565ToHex(colors[index]);
      input.dataset.themeColorIndex = String(index);
      input.disabled = !supported;
      input.setAttribute('aria-label', `${label} color`);
      const code = document.createElement('input');
      code.type = 'text';
      code.className = 'theme-color-code';
      code.value = input.value.toUpperCase();
      code.dataset.themeColorIndex = String(index);
      code.maxLength = 7;
      code.pattern = '#[0-9A-Fa-f]{6}';
      code.spellcheck = false;
      code.disabled = !supported;
      code.setAttribute('aria-label', `${label} RGB hex code`);
      input.addEventListener('pointerdown', () => {
        themePreset = 'custom';
        elements.themePreset.value = themePreset;
      });
      input.addEventListener('input', () => {
        code.removeAttribute('aria-invalid');
        themeDraft[index] = hexToRgb565(input.value);
        input.value = rgb565ToHex(themeDraft[index]);
        code.value = input.value.toUpperCase();
        themePreset = 'custom';
        elements.themePreset.value = themePreset;
        syncThemeConfigCode();
        updateThemeApplyState(supported);
      });
      code.addEventListener('input', () => {
        code.removeAttribute('aria-invalid');
        if (!/^#[0-9a-f]{6}$/i.test(code.value)) {
          code.setAttribute('aria-invalid', 'true');
          updateThemeApplyState(supported);
          return;
        }
        themeDraft[index] = hexToRgb565(code.value);
        input.value = rgb565ToHex(themeDraft[index]);
        code.value = input.value.toUpperCase();
        themePreset = 'custom';
        elements.themePreset.value = themePreset;
        syncThemeConfigCode();
        updateThemeApplyState(supported);
      });
      row.append(name, input, code);
      grid.append(row);
    }
    if (groupLabel === 'Interface') {
      const dimming = document.createElement('label');
      dimming.className = 'theme-color-control theme-dimming-control';
      dimming.htmlFor = 'theme-graph-dimming';
      const name = document.createElement('span');
      name.textContent = 'Graph dimming';
      const input = document.createElement('input');
      input.type = 'number';
      input.id = 'theme-graph-dimming';
      input.min = '0';
      input.max = '100';
      input.step = '1';
      input.value = String(themeDraftDimming);
      input.disabled = !supported;
      const suffix = document.createElement('span');
      suffix.textContent = '%';
      input.addEventListener('input', () => {
        const value = Number(input.value);
        if (!Number.isInteger(value) || value < 0 || value > 100) {
          input.setAttribute('aria-invalid', 'true');
          updateThemeApplyState(supported);
          return;
        }
        input.removeAttribute('aria-invalid');
        themeDraftDimming = value;
        themePreset = detectThemePreset(themeDraft, themeDraftDimming);
        elements.themePreset.value = themePreset;
        syncThemeConfigCode();
        updateThemeApplyState(supported);
      });
      dimming.append(name, input, suffix);
      grid.append(dimming);
    }
    group.append(title, grid);
    elements.themeColors.append(group);
  });
  syncThemeConfigCode();
  updateThemeApplyState(supported);
  if (!serial) {
    elements.themeStatus.textContent = 'Offline preview · edit and apply locally; connect to save to a device.';
  } else if (!supported) {
    elements.themeStatus.textContent = 'Theme editing is not supported by this firmware.';
  }
}

async function loadTheme() {
  if (!serial || (protocolCapabilities & CAP_THEME_CONFIG) === 0) {
    renderThemeControls();
    return;
  }
  elements.themeStatus.textContent = 'Reading theme…';
  try {
    const payload = await serial.request(OP_GET_THEME);
    if (payload.length !== 17 || payload[0] !== 0 || payload[1] !== THEME_COLORS.length ||
        payload[16] > 100) {
      throw new ProtocolError('Invalid display theme response.');
    }
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    deviceTheme = THEME_COLORS.map((_, index) => view.getUint16(2 + index * 2, true));
    deviceThemeDimming = payload[16];
    themeDraft = deviceTheme.slice();
    themeDraftDimming = deviceThemeDimming;
    themePreset = detectThemePreset(themeDraft, themeDraftDimming);
    renderThemeControls();
    elements.themeStatus.textContent = 'Loaded from device';
  } catch (error) {
    elements.themeStatus.textContent = error.message;
    logDiagnostic('GET_THEME failed.', error);
  }
}

async function applyTheme() {
  if (!serial) {
    demoTheme = themeDraft.slice();
    demoThemeDimming = themeDraftDimming;
    themePreset = detectThemePreset(demoTheme, demoThemeDimming);
    renderThemeControls();
    elements.themeStatus.textContent = 'Offline preview updated · connect to save this palette to a device.';
    return;
  }
  if (!deviceTheme || (protocolCapabilities & CAP_THEME_CONFIG) === 0 ||
      !themeCodesValid()) return;
  const button = elements.themeApply;
  button.disabled = true;
  elements.themeStatus.textContent = 'Saving theme…';
  try {
    const payload = new Uint8Array(THEME_COLORS.length * 2 + 1);
    const view = new DataView(payload.buffer);
    themeDraft.forEach((color, index) => view.setUint16(index * 2, color, true));
    payload[payload.length - 1] = themeDraftDimming;
    const response = await serial.request(OP_SET_THEME, payload);
    if (response.length !== 1 || response[0] !== 0) {
      throw new ProtocolError('Device could not save the display theme.');
    }
    deviceTheme = themeDraft.slice();
    deviceThemeDimming = themeDraftDimming;
    themePreset = detectThemePreset(deviceTheme, deviceThemeDimming);
    elements.themeStatus.textContent = 'Applied and saved on device';
    logDiagnostic('Display theme applied and saved.');
  } catch (error) {
    elements.themeStatus.textContent = error.message;
    logDiagnostic('SET_THEME failed.', error);
    await loadTheme();
  } finally {
    renderThemeControls();
  }
}

function selectThemePreset(preset) {
  themePreset = preset;
  if (THEME_PRESETS[preset]) {
    themeDraft = THEME_PRESETS[preset].colors.slice();
    themeDraftDimming = THEME_PRESETS[preset].dimming;
  }
  renderThemeControls();
  elements.themeStatus.textContent = preset === 'custom' ?
    'Custom palette selected. Edit colors, then apply.' :
    `${THEME_PRESETS[preset].label} palette selected. Apply to save it to the device.`;
}

function importThemeConfigCode() {
  const decoded = decodeThemeConfigCode(elements.themeConfigCode.value);
  const supported = !serial || (protocolCapabilities & CAP_THEME_CONFIG) !== 0;
  if (!decoded) {
    themeImportInvalid = true;
    elements.themeConfigCode.setAttribute('aria-invalid', 'true');
    elements.themeStatus.textContent = 'Invalid palette code. Expected T2: followed by 30 hexadecimal digits (T1 codes are also accepted).';
    updateThemeApplyState(supported);
    return;
  }
  themeDraft = decoded.colors;
  themeDraftDimming = decoded.dimming;
  themePreset = detectThemePreset(themeDraft, themeDraftDimming);
  elements.themePreset.value = themePreset;
  elements.themeConfigCode.removeAttribute('aria-invalid');
  themeImportInvalid = false;
  elements.themeColors.querySelectorAll('[data-theme-color-index]').forEach((input) => {
    const index = Number(input.dataset.themeColorIndex);
    if (input.type === 'color') input.value = rgb565ToHex(themeDraft[index]);
    else input.value = rgb565ToHex(themeDraft[index]).toUpperCase();
  });
  const dimmingInput = document.querySelector('#theme-graph-dimming');
  if (dimmingInput) dimmingInput.value = String(themeDraftDimming);
  syncThemeConfigCode();
  updateThemeApplyState(supported);
  elements.themeStatus.textContent = 'Palette code imported into the color fields. Apply to save it.';
}

function renderOfflinePreview() {
  if (!offlineLogsImported) logInfo = null;
  setDeviceModeActive(1);
  if (elements.deviceScreenAlwaysOn) {
    elements.deviceScreenAlwaysOn.dataset.enabled = 'false';
    elements.deviceScreenAlwaysOn.textContent = 'Screen: Auto';
  }
  if (elements.deviceFlashlight) {
    elements.deviceFlashlight.dataset.enabled = 'false';
    elements.deviceFlashlight.textContent = 'Flashlight: Off';
  }
  deviceTheme = null;
  deviceThemeDimming = null;
  themeDraft = demoTheme.slice();
  themeDraftDimming = demoThemeDimming;
  themePreset = detectThemePreset(themeDraft, themeDraftDimming);
  renderThemeControls();
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
  elements.rateFootMain.textContent = '0 CPM';
  elements.rateFootCount.textContent = '';
  elements.rateFootCount.hidden = true;
  elements.rateFoot.hidden = false;
  updateOverviewMetricDetails(0, 0, 0, 0, deviceSettings?.[10] ?? 1, 0);
  elements.rateWarmup.hidden = true;
  elements.doseWarmupSpacer.hidden = true;
  elements.rateWarmupFill.style.width = '0%';
  elements.loadMoreLogs.hidden = false;
  elements.loadMoreLogs.disabled = true;
  elements.doseLabel.textContent = 'DOSE TODAY';
  elements.dose.textContent = '0';
  elements.doseUnit.textContent = 'µSv';
  elements.doseFoot.hidden = true;
  elements.uptime.textContent = '0D 00:00:00';
  elements.debugDate.textContent = '---- -- --';
  elements.debugTime.textContent = '--:--:--';
  elements.debugCount.textContent = '0';
  elements.debugBatteryPercent.textContent = '0%';
  elements.debugBatteryDetail.textContent = '0 · 0.00V';
  elements.debugBatteryStartPercent.textContent = '0%';
  elements.debugBatteryStartDetail.textContent = '0 · 0.00V';
  elements.debugSolarPercent.textContent = '0%';
  elements.debugSolarRaw.textContent = '0';
  elements.debugUsbRaw.textContent = '0';
  for (const [percentElement, timeElement] of [
    [elements.debugChargePercent, elements.debugChargeTime],
    [elements.debugScreenPercent, elements.debugScreenTime],
    [elements.debugWakePercent, elements.debugWakeTime],
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
    const values = alert ? ['—', formatAlertDuration(0), '0 µSv', '0 µSv/h', '0 µSv/h'] :
      ['—', '0 µSv', '0 µSv/h', '0 µSv/h', `100% · ${formatCoverageDuration(demoType.period)} · full`];
    values.forEach((text, index) => {
      const cell = document.createElement('span');
      if (!alert && index === values.length - 1) cell.className = 'coverage-green';
      else if (alert ? index === 2 : index === 1) cell.className = `log-metric ${logDoseClass(demoRecord)}`;
      else if (alert ? index >= 3 : index >= 2 && index <= 3) cell.className = `log-metric ${logRateClass(0)}`;
      cell.textContent = text;
      record.append(cell);
    });
    const total = document.createElement('div');
    total.className = 'log-grid log-record log-total';
    const totalValues = alert ?
      ['TOTAL (1)', formatAlertDuration(0), '0 µSv', '0 µSv/h', '0 µSv/h'] :
      ['TOTAL (1)', '0 µSv', '0 µSv/h', '0 µSv/h', `${formatCoverageDuration(demoType.period)} total`];
    totalValues.forEach((text, index) => {
      const cell = document.createElement('span');
      if (alert ? index === 2 : index === 1) cell.className = `log-metric ${logDoseClass(demoRecord)}`;
      else if (alert ? index >= 3 : index >= 2 && index <= 3) cell.className = `log-metric ${logRateClass(0)}`;
      else if (!alert && index === 4) cell.className = 'coverage-green';
      cell.textContent = text;
      total.append(cell);
    });
    elements.logContent.append(header, total, record);
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
  if (offlineLogsImported) activateOfflineImportedLogs();
  else renderDemoLog(selectedLogType);
  updateLogRangeControls();
  updateLogActions();
  elements.settingsDeviceTime.textContent = '--:--:--';
  elements.settingsDeviceDate.textContent = '---- -- --';
  elements.settingsClockDifference.textContent = '—';
  elements.settingsSystemTime.textContent = new Date().toLocaleTimeString('en-GB');
  elements.settingsSystemDate.textContent = new Date().toLocaleDateString('en-CA');
  elements.settingsClockStatus.textContent = '';
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

function updateDeviceControls() {
  const supported = !!serial && (protocolCapabilities & CAP_DEVICE_ACTION) !== 0;
  const controls = [elements.deviceWakeScreen, elements.deviceFlashlight,
    elements.deviceScreenAlwaysOn,
    ...elements.deviceModeButtons].filter(Boolean);
  controls.forEach((control) => { control.disabled = !supported; });
  if (elements.deviceControlsStatus) {
    if (!supported) elements.deviceControlsStatus.textContent = 'Connect to control the device.';
    else if (!elements.deviceControlsStatus.textContent ||
             elements.deviceControlsStatus.textContent === 'Connect to control the device.')
      elements.deviceControlsStatus.textContent = 'Ready';
  }
}

async function runDeviceControlAction(action, value, button, successText) {
  if (!serial || (protocolCapabilities & CAP_DEVICE_ACTION) === 0) return;
  button.disabled = true;
  elements.deviceControlsStatus.textContent = 'Sending…';
  try {
    const result = await serial.request(OP_DEVICE_ACTION,
      Uint8Array.of(action, value));
    if (result.length !== 1 || result[0] !== 0)
      throw new ProtocolError('Device rejected this action.');
    elements.deviceControlsStatus.textContent = successText;
    return true;
  } catch (error) {
    elements.deviceControlsStatus.textContent = error.message;
    return false;
  } finally {
    updateDeviceControls();
  }
}

function setDeviceModeActive(mode) {
  deviceIndicationMode = mode;
  elements.deviceModeButtons.forEach((control) => {
    control.dataset.active = mode !== null &&
      control.dataset.mode === String(mode) ? 'true' : 'false';
  });
  if (mode === 2 && serial && !fastRatePollTimer) {
    fastRatePollTimer = setInterval(() => {
      if (!serial || deviceIndicationMode !== 2) {
        clearInterval(fastRatePollTimer);
        fastRatePollTimer = 0;
      } else if (pollTimer) {
        refreshStatus(true);
      }
    }, 100);
  } else if (mode !== 2 && fastRatePollTimer) {
    clearInterval(fastRatePollTimer);
    fastRatePollTimer = 0;
  }
}

async function loadSettings() {
  if (!serial) return;
  if ((protocolCapabilities & (CAP_GET_SETTINGS | CAP_SET_SETTING)) !==
      (CAP_GET_SETTINGS | CAP_SET_SETTING)) {
    elements.settingsStatus.textContent = '';
    renderSettings();
    await loadTheme();
    return;
  }
  elements.settingsStatus.textContent = 'Reading…';
  try {
    const payload = await serial.request(OP_GET_SETTINGS, Uint8Array.of(1));
    if (payload.length < 2 || payload[0] !== 0 ||
        ![21, 22, 24, 28, 29, 30, 31].includes(payload[1]) ||
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
    await loadTheme();
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
      rateHistoryLevels.length = 0;
      rateHistoryPulses.length = 0;
      rateHistoryTimes.length = 0;
      previousRateCount = null;
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
    'Clear all log records in EEPROM and SPI? SPI is erased only if every non-empty sector matches the log format; unrecognized contents are left intact. Daily and lifetime dose counters are reset.',
    deviceSettings?.[13] === 1 ?
      'Clear inactive EEPROM logs? Active SPI logs will remain.' :
      deviceSettings?.[13] === 0 ?
        'Clear inactive SPI logs? The chip is erased only if its contents match the log format; unrecognized contents are left intact.' :
        'LOG MEMORY is OFF: clear all logs in EEPROM and SPI? SPI is erased only if every non-empty sector matches the log format; unrecognized contents are left intact.',
    'Erase the entire EEPROM, including settings and dose checkpoints? The device will restart.',
    'Erase SPI log contents? The chip is erased only if every non-empty sector matches the log format; unrecognized contents are left intact.',
    'Erase all EEPROM and SPI data? The SPI chip is erased only if every non-empty sector matches the log format; unrecognized contents are left intact.',
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
    if (mode === 3) return `${formatCps(cpm)} CPS`;
  } else {
    if (mode === 2) return '';
    if (mode === 0) return `${formatGroupedNumber(cpm)} CPM`;
    if (mode === 1) return `${formatCps(cpm)} CPS`;
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

function updateOverviewMetricDetails(cpm, uptimePulses,
    dayPulses, totalPulses, ratePrefix, rateUsvh = cpm * 0.00666) {
  const rateSv = formatRadiation(rateUsvh, ratePrefix, 'Sv', true);
  const rateR = formatRadiation(rateUsvh * 100, ratePrefix, 'R', true);
  document.querySelector('#overview-rate-cpm').textContent = formatGroupedNumber(Math.round(cpm));
  document.querySelector('#overview-rate-cps').textContent = formatCps(Math.round(cpm));
  document.querySelector('#overview-rate-sv-label').textContent = 'Sv/h';
  document.querySelector('#overview-rate-sv').textContent = `${rateSv.value} ${rateSv.unit}`;
  document.querySelector('#overview-rate-r-label').textContent = 'R/h';
  document.querySelector('#overview-rate-r').textContent = `${rateR.value} ${rateR.unit}`;

  const periods = [uptimePulses, dayPulses, totalPulses];
  const doseUnits = [
    {key: 'sv', symbol: 'Sv', factor: 1},
    {key: 'r', symbol: 'R', factor: 100},
  ];
  for (const doseUnit of doseUnits) {
    periods.forEach((pulses, periodIndex) => {
      const doseUsv = pulses * (400 / 3600) / 1000 * doseUnit.factor;
      const formatted = formatRadiation(doseUsv, 1, doseUnit.symbol, false, 6);
      const period = ['uptime', 'day', 'total'][periodIndex];
      document.querySelector(`#overview-dose-${doseUnit.key}-${period}`).textContent = formatted.value;
    });
  }
  periods.forEach((pulses, periodIndex) => {
    const period = ['uptime', 'day', 'total'][periodIndex];
    document.querySelector(`#overview-dose-cnt-${period}`).textContent = formatGroupedNumber(pulses);
  });
}

function setMetricExpanded(button, expanded) {
  for (const [toggle, details] of [
    [elements.rateExpand, elements.rateDetails],
    [elements.doseExpand, elements.doseDetails],
  ]) {
    const label = toggle.id.startsWith('rate') ? 'rate' : 'dose';
    toggle.setAttribute('aria-expanded', String(expanded));
    toggle.setAttribute('aria-label', `${expanded ? 'Collapse' : 'Expand'} ${label} details`);
    toggle.title = toggle.getAttribute('aria-label');
    toggle.textContent = expanded ? '-' : '+';
    details.hidden = !expanded;
    toggle.closest('.metric').classList.toggle('is-expanded', expanded);
  }
}

function handleStatus(payload) {
  if (![25, 29, 31, 35, 47].includes(payload.length) || payload[0] !== 0) throw new ProtocolError('Device rejected GET_STATUS or returned an unknown format.');
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const uptimeSeconds = view.getUint32(1, true);
  const count = view.getUint32(5, true);
  const reportedRate = view.getUint32(9, true);
  const hasTenthsRate = (protocolCapabilities & CAP_RATE_CPM_TENTHS) !== 0;
  const cpm = hasTenthsRate ? reportedRate / 10 : reportedRate;
  const rate100000ths = hasTenthsRate ?
    Math.floor((reportedRate * 666 + 5) / 10) : reportedRate * 666;
  const baseRateUsvh = rate100000ths / 100000;
  const displayedCpm = Math.round(cpm);
  const doseNsvWhole = view.getUint32(13, true);
  const doseFraction = payload.length >= 31 ? view.getUint16(29, true) : 0;
  const doseNsv = doseNsvWhole + doseFraction / 3600;
  const batteryRaw = view.getUint16(17, true);
  const solarRaw = view.getUint16(19, true);
  const usbRaw = view.getUint16(21, true);
  const countTotal = payload.length >= 29 ? view.getUint32(25, true) : count;
  const lifetimePulses = payload.length >= 35 ? view.getUint32(31, true) : countTotal;
  const screenSeconds = payload.length >= 47 ? view.getUint32(35, true) : null;
  const sleepSeconds = payload.length >= 47 ? view.getUint32(39, true) : null;
  const usbSeconds = payload.length >= 47 ? view.getUint32(43, true) : null;
  const statusFlags = view.getUint16(23, true);
  if (hasTenthsRate) {
    const indicationMode = (statusFlags >>> 2) & 0x03;
    setDeviceModeActive(indicationMode <= 2 ? indicationMode : null);
  }
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
    (displayedCpm * 666 > 90000 ? 'red' :
      (displayedCpm * 666 >= 40000 ? 'yellow' : 'green'));
  const levelClass = `level-${rateLevel}`;
  elements.rateDose.classList.remove('level-blue', 'level-green', 'level-yellow', 'level-red');
  elements.rateDose.classList.add(levelClass);
  const warmupPulses = Math.min(countTotal, 30);
  elements.rateWarmup.hidden = rateLevel !== 'blue';
  elements.doseWarmupSpacer.hidden = rateLevel !== 'blue';
  elements.rateWarmup.setAttribute('aria-valuenow', String(warmupPulses));
  elements.rateWarmupFill.style.width = `${(warmupPulses / 30) * 100}%`;
  const uptime = formatUptime(uptimeSeconds);
  const unit = deviceSettings?.[9] ?? 0;
  const ratePrefix = deviceSettings?.[10] ?? 1;
  const dosePrefix = deviceSettings?.[11] ?? 1;
  const rateUsvh = baseRateUsvh * (unit === 2 ? 100 : 1);
  const doseUsv = doseNsv / 1000 * (unit === 2 ? 100 : 1);
  updateOverviewMetricDetails(cpm, uptimePulses, dayPulses,
    lifetimePulses, ratePrefix, baseRateUsvh);
  // When the user is looking at an interior slice of the history, keep that
  // slice fixed as new samples arrive. Only the Y scale may change. A view at
  // the live edge (scroll = 0) continues following "now"; the old edge is
  // clamped normally when the buffer advances.
  const rateChartIsHistorical = rateChartScrollSeconds > 0;
  const dosePeriodLabels = ['DOSE UPTIME', 'DOSE TODAY', 'DOSE TOTAL'];
  elements.doseLabel.textContent = dosePeriodLabels[dosePeriod] || 'DOSE TODAY';

  if (unit === 1) {
    elements.rateLabel.textContent = 'RATE';
    elements.rateDose.textContent = formatGroupedNumber(displayedCpm);
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
  const rateRow = rateRowText(rateRowMode, unit, displayedCpm, rateUsvh,
    deviceSettings?.[10] ?? 1);
  elements.rateFootMain.textContent = rateRow;
  elements.rateFootCount.textContent = rateLevel === 'blue' ? `${formatGroupedNumber(warmupPulses)} CNT` : '';
  elements.rateFootCount.hidden = rateLevel !== 'blue';
  elements.rateFoot.hidden = rateRow.length === 0 && rateLevel !== 'blue';
  let doseRow = doseRowText(unit, dosePeriod, doseRowMode, cpm, uptimeSeconds,
    uptimePulses, dayPulses, lifetimePulses, dosePrefix);
  elements.doseFoot.textContent = doseRow;
  elements.doseFoot.hidden = doseRow.length === 0;
  elements.doseFoot.classList.toggle('align-with-rate-warmup', rateLevel === 'blue');
  elements.debugCount.textContent = formatGroupedNumber(countTotal);
  elements.uptime.textContent = `${uptime.days}D ${uptime.clock}`;
  updateDeviceDateTime(`${uptime.days}D ${uptime.clock}`);
  // count_total is the detector counter used by the device while HV is on.
  // It follows the actual LED flashes and does not depend on the selected
  // dose period or on the calendar day boundary.
  const pulseDelta = previousRateCount === null || countTotal < previousRateCount ?
    0 : countTotal - previousRateCount;
  previousRateCount = countTotal;
  rateHistory.push(unit === 1 ? displayedCpm : rateUsvh);
  rateHistoryLevels.push(rateLevel);
  rateHistoryPulses.push(pulseDelta);
  rateHistoryTimes.push(logInfo?.calendarValid ? logInfo.calendarSeconds :
    Math.floor((Date.now() - Date.UTC(2000, 0, 1)) / 1000));
  if (rateChartIsHistorical) rateChartScrollSeconds += 1;
  rateHistoryUnit = unit === 1 ? 'CPM' :
    formatRadiation(rateUsvh, ratePrefix, unit === 2 ? 'R' : 'Sv', true).unit;
  elements.rateChart.setAttribute('aria-label', `Recent rate history (${RATE_CHART_BUFFER_SECONDS} seconds; zoom and scroll available)`);
  if (rateHistory.length > RATE_CHART_BUFFER_SECONDS) rateHistory.shift();
  if (rateHistoryLevels.length > RATE_CHART_BUFFER_SECONDS) rateHistoryLevels.shift();
  if (rateHistoryPulses.length > RATE_CHART_BUFFER_SECONDS) rateHistoryPulses.shift();
  if (rateHistoryTimes.length > RATE_CHART_BUFFER_SECONDS) rateHistoryTimes.shift();
  drawRateChart();
  const solarPercent = Math.min(100, Math.floor(solarRaw / 31));
  elements.solarPercent.textContent = `${solarPercent}%`;
  elements.solarPercent.classList.toggle('solar-warn', solarPercent > 49 && solarPercent <= 79);
  elements.solarPercent.classList.toggle('solar-good', solarPercent > 79);
  const batteryPercent = percentFromAdc(batteryRaw, 1300, 2000);
  elements.batteryPercent.textContent = `${batteryPercent}%`;
  elements.debugBatteryPercent.textContent = `${batteryPercent}%`;
  elements.debugBatteryStartPercent.textContent = batteryStartRaw === null ? '—' :
    `${percentFromAdc(batteryStartRaw, 1300, 2000)}%`;
  elements.debugBatteryDetail.textContent =
    `${formatGroupedNumber(batteryRaw)} · ${voltageFromPowerAdc(batteryRaw)}`;
  elements.debugBatteryStartDetail.textContent = batteryStartRaw === null ? '—' :
    `${formatGroupedNumber(batteryStartRaw)} · ${voltageFromPowerAdc(batteryStartRaw)}`;
  elements.debugSolarPercent.textContent = `${solarPercent}%`;
  elements.debugSolarRaw.textContent = formatGroupedNumber(solarRaw);
  elements.debugUsbRaw.textContent = formatGroupedNumber(usbRaw);
  for (const [seconds, percentElement, timeElement] of [
    [usbSeconds, elements.debugChargePercent, elements.debugChargeTime],
    [screenSeconds, elements.debugScreenPercent, elements.debugScreenTime],
    [sleepSeconds === null ? null : Math.max(0, uptimeSeconds - sleepSeconds),
      elements.debugWakePercent, elements.debugWakeTime],
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

async function refreshStatus(quiet = false) {
  if (!serial || polling) return;
  polling = true;
  try {
    const payload = await serial.request(OP_GET_STATUS);
    handleStatus(payload);
    if (!quiet) showNotice(deviceIndicationMode === 2 ?
      'Connected. Search readings update every 100 ms.' :
      'Connected. Readings update once per second.', 'success');
  } catch (error) {
    if (!quiet) showNotice(error.message, 'error');
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
elements.rateExpand.addEventListener('click', () =>
  setMetricExpanded(elements.rateExpand,
    elements.rateExpand.getAttribute('aria-expanded') !== 'true'));
elements.doseExpand.addEventListener('click', () =>
  setMetricExpanded(elements.doseExpand,
    elements.doseExpand.getAttribute('aria-expanded') !== 'true'));
elements.debugTab.addEventListener('click', () => selectPage('debug'));
elements.settingsTab.addEventListener('click', () => selectPage('settings'));
document.querySelector('#settings-refresh').addEventListener('click', refreshSettingsPage);
elements.themeApply.addEventListener('click', applyTheme);
elements.themeConfigCode.addEventListener('input', importThemeConfigCode);
elements.themePreset.addEventListener('change', () =>
  selectThemePreset(elements.themePreset.value));
document.querySelector('#debug-refresh').addEventListener('click', refreshDebugPage);
elements.syncClock.addEventListener('click', synchronizeDeviceClock);
elements.logsTab.addEventListener('click', () => selectPage('logs'));
elements.toggleLogsFullscreen.addEventListener('click', toggleLogsFullscreen);
elements.exitLogsFullscreen.addEventListener('click', () => setLogsFullscreen(false));
window.addEventListener('keydown', (event) => {
  if (event.key === 'Escape' && elements.logsPage.classList.contains('is-fullscreen')) {
    setLogsFullscreen(false);
  }
});
elements.loadMoreLogs.addEventListener('click', loadMoreLogRecords);
elements.loadLogSelection.addEventListener('click', () => {
  if (logPageLoading) {
    logStopRequested = true;
    updateLogRangeControls();
    return;
  }
  loadLogSelection();
});
elements.clearDiagnostics.addEventListener('click', () => {
  diagnosticsLog.length = 0;
  elements.diagnosticsOutput.textContent = '';
});
elements.diagnosticsPanel.addEventListener('toggle', () => {
  if (elements.diagnosticsPanel.open)
    elements.diagnosticsOutput.textContent = diagnosticsLog.join('\n');
});
elements.copyDiagnostics.addEventListener('click', async () => {
  try {
    await navigator.clipboard.writeText(diagnosticsLog.join('\n'));
    logDiagnostic('Diagnostics copied to clipboard.');
  } catch (error) {
    logDiagnostic('Could not copy diagnostics', error);
  }
});
elements.logRangeMode.addEventListener('change', updateLogRangeControls);
elements.logPageSize.addEventListener('change', () => {
  logDisplayPage = 0;
  elements.logContent.scrollTop = 0;
  renderLogSummary();
});
elements.logPagePrevious.addEventListener('click', () => {
  logDisplayPage = Math.max(0, logDisplayPage - 1);
  elements.logContent.scrollTop = 0;
  renderLogSummary();
});
elements.logPageNext.addEventListener('click', () => {
  ++logDisplayPage;
  elements.logContent.scrollTop = 0;
  renderLogSummary();
});
elements.logPageNumber.addEventListener('change', () => {
  logDisplayPage = Number(elements.logPageNumber.value) || 0;
  elements.logContent.scrollTop = 0;
  renderLogSummary();
});
elements.logFillMetric.addEventListener('change', () => {
  logFillMetric = elements.logFillMetric.value;
  if (serial && logInfo) renderLogSummary();
  else demoLogRenderer?.(selectedLogType);
});
elements.exportLogsCsv.addEventListener('click', exportLogsCsv);
elements.exportAllLogsCsv.addEventListener('click', exportAllLogsCsv);
elements.importLogs.addEventListener('click', () => elements.importLogsFile.click());
elements.importLogsFile.addEventListener('change', async () => {
  const file = elements.importLogsFile.files?.[0];
  if (!file) return;
  elements.importLogs.disabled = true;
  elements.logsStatus.textContent = `Reading ${file.name}…`;
  try {
    await importLogFile(file);
  } catch (error) {
    elements.logsStatus.textContent = `Import failed: ${error.message}`;
  } finally {
    elements.importLogs.disabled = false;
    elements.importLogsFile.value = '';
    updateLogActions();
  }
});
elements.firmwareTab.addEventListener('click', () => selectPage('firmware'));
elements.rebootFirmware.addEventListener('click', rebootToFirmware);
elements.rebootBootloader.addEventListener('click', rebootToBootloader);
elements.firmwareFile.addEventListener('change', () => {
  beginFirmwareLogAction('Firmware image check');
  void loadFirmwareImage();
});
elements.flashLocalBuild.addEventListener('click', flashLocalBuild);
elements.quickFlashFirmware?.addEventListener('click', quickFlashEmbeddedFirmware);
elements.clearFirmware.addEventListener('click', clearFirmwareSelection);
elements.flashFirmware.addEventListener('click', () => flashSelectedFirmware());
elements.dumpFlash.addEventListener('click', () => {
  void dumpRomMemory('FLASH', STM32_FLASH_BASE, STM32_FLASH_SIZE, 'flash', false, false, true);
});
elements.restoreManualFlash?.addEventListener('click', restoreManualFlashBackup);
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
elements.eepromImageFile.addEventListener('change', loadEepromImage);
elements.clearEepromImage.addEventListener('click', clearEepromImageSelection);
elements.writeEepromImage.addEventListener('click', writeEepromImage);
elements.spiImageFile.addEventListener('change', loadSpiImage);
elements.clearSpiImage.addEventListener('click', clearSpiImageSelection);
elements.writeSpiImage.addEventListener('click', writeSpiImage);
elements.manualTab.addEventListener('click', () => selectPage('manual'));
elements.aboutTab.addEventListener('click', () => selectPage('about'));
elements.clearFirmwareLog.addEventListener('click', () => {
  elements.firmwareLog.replaceChildren();
  activeFirmwareLogAction = null;
});
window.addEventListener('hashchange', () => {
  const page = window.location.hash.slice(1);
  selectPage(page || 'overview');
});
selectPage(window.location.hash.slice(1) || 'overview');
window.addEventListener('resize', drawRateChart);
detectLocalBuild();
loadLatestFlashBackup();
  loadMemoryDownloads();
if (typeof globalThis.EMBEDDED_FIRMWARE_B64 === 'string' && elements.embeddedQuickFlash) {
  elements.embeddedQuickFlash.hidden = false;
  elements.embeddedQuickFlash.open = true;
  elements.firmwareTools.open = false;
}
renderEmbeddedStockFirmware();

let activeFirmwareLogAction = null;

function beginFirmwareLogAction(title) {
  for (const previous of elements.firmwareLog.querySelectorAll('.firmware-log-action')) {
    previous.open = false;
  }
  const details = document.createElement('details');
  details.className = 'firmware-log-action';
  details.open = true;
  const summary = document.createElement('summary');
  const time = document.createElement('span');
  time.className = 'log-action-time';
  time.textContent = new Date().toLocaleTimeString();
  const label = document.createElement('span');
  label.textContent = title;
  summary.append(time, label);
  const lines = document.createElement('pre');
  lines.className = 'firmware-log-lines';
  details.append(summary, lines);
  elements.firmwareLog.append(details);
  activeFirmwareLogAction = {details, lines};
  elements.firmwareLog.scrollTop = elements.firmwareLog.scrollHeight;
}

function appendFirmwareLog(message, level = 'normal') {
  logDiagnostic(`Firmware: ${message}${level === 'normal' ? '' : ` [${level}]`}`);
  if (!activeFirmwareLogAction) beginFirmwareLogAction('Activity');
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
  activeFirmwareLogAction.lines.append(line);
  elements.firmwareLog.scrollTop = elements.firmwareLog.scrollHeight;
}

function updateStockFirmwareButtons() {
  if (!elements.stockFirmwareList) return;
  const disabled = dumpBusy || firmwareFlashBusy || firmwareBackupBusy;
  for (const button of elements.stockFirmwareList.querySelectorAll('button')) {
    button.disabled = disabled;
  }
}

function renderEmbeddedStockFirmware() {
  const images = globalThis.EMBEDDED_STOCK_FIRMWARES;
  if (!Array.isArray(images) || images.length === 0 || !elements.stockFirmwareSection) return;
  elements.stockFirmwareSection.hidden = false;
  for (const entry of images) {
    if (!Array.isArray(entry) || entry.length !== 2 ||
        typeof entry[0] !== 'string' || typeof entry[1] !== 'string') continue;
    const [filename, base64] = entry;
    const label = filename.replace(/\.hex$/i, '').replace(/-\(only-Windows\)/i, ' · Windows').replaceAll('-', ' ');
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'button';
    button.textContent = `Flash ${label}`;
    button.title = filename;
    button.addEventListener('click', () => flashEmbeddedStockFirmware(filename, base64));
    elements.stockFirmwareList.append(button);
  }
  renderStockFirmwareSources(globalThis.EMBEDDED_STOCK_SOURCE_LINKS);
  updateStockFirmwareButtons();
}

function renderStockFirmwareSources(sources) {
  const container = elements.stockFirmwareSources;
  if (!container || !Array.isArray(sources) || sources.length === 0) return;
  const heading = document.createElement('h4');
  heading.textContent = 'Sources and downloads';
  const list = document.createElement('ul');
  for (const source of sources) {
    if (!source || typeof source.label !== 'string' ||
        typeof source.description !== 'string' || typeof source.url !== 'string') continue;
    let parsedUrl;
    try {
      parsedUrl = new URL(source.url);
    } catch {
      continue;
    }
    if (parsedUrl.protocol !== 'https:') continue;
    const item = document.createElement('li');
    const link = document.createElement('a');
    link.href = parsedUrl.href;
    link.target = '_blank';
    link.rel = 'noopener noreferrer';
    link.textContent = source.label;
    const description = document.createElement('span');
    description.textContent = source.description;
    item.append(link, description);
    list.append(item);
  }
  if (!list.childElementCount) return;
  container.replaceChildren(heading, list);
  container.hidden = false;
}

async function flashEmbeddedStockFirmware(filename, base64) {
  if (dumpBusy || firmwareFlashBusy || firmwareBackupBusy) return;
  const label = filename.replace(/\.hex$/i, '').replace(/-\(only-Windows\)/i, ' · Windows').replaceAll('-', ' ');
  beginFirmwareLogAction(`Stock firmware · ${label}`);
  try {
    const binary = atob(base64);
    const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
    await loadFirmwareImage(new File([bytes], filename, {type: 'text/plain'}));
    if (!selectedFirmwareImage || selectedFirmwareImage.identity !== 'stock') {
      throw new Error('The embedded image did not pass stock firmware classification.');
    }
    await flashSelectedFirmware(true);
  } catch (error) {
    appendFirmwareLog(`Stock firmware flash failed: ${error.message}`, 'error');
    setRebootStatus(`Stock firmware flash failed: ${error.message}`, 'error');
  }
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
  if (KNOWN_STOCK_IMAGE_SHA256S.has(hash)) return 'stock';
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
  beginFirmwareLogAction('Local build flash');
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
    await flashSelectedFirmware(true);
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
  beginFirmwareLogAction('Quick flash');
  try {
    const binary = atob(globalThis.EMBEDDED_FIRMWARE_B64);
    const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
    const image = new File([bytes], 'embedded-firmware.bin', { type: 'application/octet-stream' });
    await loadFirmwareImage(image);
    if (!selectedFirmwareImage) throw new Error('The embedded firmware failed image validation.');
    await flashSelectedFirmware(true);
  } catch (error) {
    appendFirmwareLog(`Quick flash failed: ${error.message}`, 'error');
    setRebootStatus(`Quick flash failed: ${error.message}`, 'error');
  }
}

function clearFirmwareSelection() {
  beginFirmwareLogAction('Clear firmware selection');
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
  const actionBusy = busy || firmwareFlashBusy || firmwareBackupBusy;
  elements.dumpFlash.disabled = actionBusy || !!spiRecoveryBackup;
  if (elements.restoreManualFlash) elements.restoreManualFlash.disabled = actionBusy || !manualFlashBackup;
  elements.dumpEeprom.disabled = actionBusy;
  elements.eraseEeprom.disabled = actionBusy;
  elements.eepromRangePreset.disabled = actionBusy;
  elements.eepromAddressFrom.disabled = actionBusy;
  elements.eepromAddressTo.disabled = actionBusy;
  elements.eraseSpi.disabled = actionBusy;
  elements.spiRangePreset.disabled = actionBusy;
  if (elements.spiDumpMethod) elements.spiDumpMethod.disabled = actionBusy;
  elements.spiAddressFrom.disabled = actionBusy;
  elements.spiAddressTo.disabled = actionBusy;
  elements.eepromImageFile.disabled = actionBusy;
  elements.clearEepromImage.disabled = actionBusy;
  elements.spiImageFile.disabled = actionBusy;
  elements.clearSpiImage.disabled = actionBusy;
  elements.rebootFirmware.disabled = actionBusy;
  elements.rebootBootloader.disabled = actionBusy;
  elements.firmwareFile.disabled = actionBusy;
  elements.clearFirmware.disabled = actionBusy;
  elements.firmwareVerify.disabled = actionBusy;
  elements.flashFirmware.disabled = actionBusy || !selectedFirmwareImage;
  if (elements.quickFlashFirmware) elements.quickFlashFirmware.disabled = actionBusy;
  if (elements.flashLocalBuild) elements.flashLocalBuild.disabled = actionBusy;
  updateEepromWriteButton();
  updateSpiWriteButton();
  updateSpiDumpAvailability();
  updateStockFirmwareButtons();
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
  elements.writeEepromImage.disabled = dumpBusy || firmwareFlashBusy || firmwareBackupBusy || !fits;
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
  elements.writeSpiImage.disabled = dumpBusy || firmwareFlashBusy || firmwareBackupBusy || !fits || !!spiRecoveryBackup;
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

async function dumpRomMemory(label, address, length, filename,
  allowBackupPreflight = false, savePermanentBackup = false, saveManualBackup = false) {
  if (dumpBusy || firmwareFlashBusy || (firmwareBackupBusy && !allowBackupPreflight) ||
      (label === 'FLASH' && spiRecoveryBackup)) return false;
  if (!allowBackupPreflight) beginFirmwareLogAction(`${label} dump`);
  let port = null;
  try {
    ({ port } = await selectFirmwarePort());
  } catch (error) {
    setDumpStatus(`Could not select the USB port: ${error.message}`, 'error');
    return false;
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
    return false;
  }
  let backupWarning = '';
  let backupSaved = false;
  if (label === 'FLASH' && savePermanentBackup) {
    if (latestFlashBackup?.bytes?.length === STM32_FLASH_SIZE) {
      // Keep the one-time original backup immutable; a manual dump is still downloaded below.
      backupSaved = true;
      appendFirmwareLog('Existing permanent FLASH backup retained; this additional dump will not replace it.');
    } else {
      try {
        await saveLatestFlashBackup(bytes, dumpFilename);
        backupSaved = true;
      }
      catch (error) {
        backupWarning = ` IndexedDB backup failed: ${error.message}`;
        appendFirmwareLog(`FLASH dump downloaded but could not be saved in IndexedDB: ${error.message}`, 'warning');
      }
    }
  } else if (label === 'FLASH' && saveManualBackup) {
    try {
      await saveManualFlashBackup(bytes, dumpFilename);
      backupSaved = true;
      appendFirmwareLog('Manual FLASH dump saved separately in browser storage.');
    } catch (error) {
      backupWarning = ` Browser copy failed: ${error.message}`;
      appendFirmwareLog(`Manual FLASH dump downloaded but could not be saved in browser storage: ${error.message}`, 'warning');
    }
  }
  if (label === 'EEPROM') {
    await rememberMemoryDump(bytes, dumpFilename, 'eeprom', address - STM32_EEPROM_BASE, 'eeprom');
  }
  const memory = label === 'EEPROM' ? 'eeprom' : 'flash';
  const source = label === 'FLASH' ? (savePermanentBackup ? 'original' : saveManualBackup ? 'manual' : 'dump') : 'dump';
  const downloadName = normalizedDumpFilename({memory, source, name: dumpFilename,
    startAddress: label === 'EEPROM' ? address - STM32_EEPROM_BASE : 0, size: bytes.length,
    savedAt: Date.now(), firmwareKind: memory === 'flash' ? await classifyFlashDump(bytes) : undefined});
  downloadDump(bytes, downloadName);
  setDumpStatus(restartFailure
    ? `${label} dump saved as ${downloadName}, but firmware restart failed: ${restartFailure.message}${backupWarning}`
    : `${label} dump saved as ${downloadName} (${length.toLocaleString()} bytes).${backupWarning}`,
  restartFailure || backupWarning ? 'warning' : 'success');
  return label !== 'FLASH' || (!savePermanentBackup && !saveManualBackup) || backupSaved;
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
  beginFirmwareLogAction(`EEPROM ${operationLabel.toLowerCase()}`);

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
    const request = indexedDB.open(SPI_RECOVERY_DB, 3);
    request.onupgradeneeded = () => {
      if (!request.result.objectStoreNames.contains('backup')) request.result.createObjectStore('backup');
      if (!request.result.objectStoreNames.contains('spi-dumps')) request.result.createObjectStore('spi-dumps');
      if (!request.result.objectStoreNames.contains('downloads')) request.result.createObjectStore('downloads');
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('Could not open backup storage.'));
  });
}

async function rememberMemoryDump(bytes, name, memory, startAddress = 0, source = 'dump', id = null) {
  const firmwareKind = memory === 'flash' ? await classifyFlashDump(bytes) : undefined;
  const record = {id: id || `${Date.now()}-${Math.random().toString(36).slice(2)}`,
    bytes: bytes.slice().buffer, name, memory, startAddress, source, savedAt: Date.now(), size: bytes.length,
    firmwareKind};
  let retained = true;
  try {
    const db = await openSpiRecoveryDatabase();
    try {
      await new Promise((resolve, reject) => {
        const tx = db.transaction('downloads', 'readwrite');
        tx.objectStore('downloads').put(record, record.id);
        tx.oncomplete = resolve;
        tx.onerror = () => reject(tx.error || new Error('Could not save memory dump.'));
        tx.onabort = () => reject(tx.error || new Error('Memory dump save was aborted.'));
      });
    } finally { db.close(); }
  } catch (error) {
    retained = false;
    appendFirmwareLog(`Could not retain ${memory.toUpperCase()} dump for later download: ${error.message}`, 'warning');
  }
  const metadata = {...record};
  delete metadata.bytes;
  if (!retained) metadata.volatileBytes = bytes.slice();
  memoryDownloads = [metadata, ...memoryDownloads.filter((item) => item.id !== metadata.id)];
  renderMemoryDownloads();
}

function rememberSpiDump(bytes, name, startAddress) {
  return rememberMemoryDump(bytes, name, 'spi', startAddress, 'spi');
}

function memoryDumpLabel(item) {
  if (item.source === 'recovery') return 'Pending FLASH recovery';
  const labels = {original: 'FLASH auto dump', legacy: 'FLASH auto dump',
    manual: 'FLASH dump', eeprom: 'EEPROM dump', spi: 'SPI dump'};
  const base = item.source === 'original' || item.source === 'legacy' || item.source === 'manual'
    ? labels[item.source] : labels[item.memory] || `${item.memory.toUpperCase()} dump`;
  return item.memory === 'flash' ? `${base} · ${item.firmwareKind || 'Unknown firmware'}` : base;
}

function normalizedDumpFilename(item) {
  const memory = item.memory || 'flash';
  const automatic = ['original', 'legacy', 'recovery'].includes(item.source);
  const firmwareKind = item.firmwareKind === 'Our firmware' ? 'ours'
    : item.firmwareKind === 'Stock firmware' ? 'stock' : 'unknown';
  const size = Number(item.size || item.bytes?.byteLength || 0);
  const startAddress = Number(item.startAddress || 0);
  const match = /(\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2})/.exec(item.name || '');
  const date = match?.[1] || (() => {
    const value = new Date(item.savedAt || Date.now());
    const pad = (part) => String(part).padStart(2, '0');
    return `${value.getFullYear()}-${pad(value.getMonth() + 1)}-${pad(value.getDate())}_` +
      `${pad(value.getHours())}-${pad(value.getMinutes())}-${pad(value.getSeconds())}`;
  })();
  if (memory !== 'flash') return `${memory}-${startAddress}-${startAddress + size - 1}-${date}.bin`;
  return `${memory}${automatic ? '-auto' : ''}-${firmwareKind}-${date}.bin`;
}

async function classifyFlashDump(bytes) {
  try {
    if (!bytes?.length || bytes.length > STM32_FLASH_SIZE) return 'Unknown firmware';
    const image = new Uint8Array(STM32_FLASH_SIZE).fill(0xff);
    const present = new Uint8Array(STM32_FLASH_SIZE);
    image.set(bytes);
    present.fill(1, 0, bytes.length);
    const identity = await classifyFirmwareImage(image, present);
    if (identity === 'stock') return 'Stock firmware';
    if (identity.startsWith('ours')) return 'Our firmware';
  } catch { /* Unknown firmware is the safe fallback when classification is unavailable. */ }
  return 'Unknown firmware';
}

function renderMemoryDownloads() {
  const container = elements.spiDownloadsList;
  if (!container) return;
  container.replaceChildren();
  const visibleDownloads = memoryDownloads.filter((item) => item.source !== 'recovery');
  if (spiRecoveryBackup) {
    const pending = memoryDownloads.find((item) => item.source === 'recovery' &&
      item.name === spiRecoveryBackup.name);
    visibleDownloads.unshift(pending || {id: 'pending-recovery-memory', name: spiRecoveryBackup.name,
      memory: 'flash', source: 'recovery', savedAt: Date.now(), size: spiRecoveryBackup.bytes.length,
      bytes: spiRecoveryBackup.bytes});
  }
  for (const item of visibleDownloads) {
    const row = document.createElement('article');
    row.className = 'spi-download-item';
    const info = document.createElement('div');
    info.className = 'spi-download-info';
    const title = document.createElement('strong');
    title.textContent = memoryDumpLabel(item);
    const description = document.createElement('span');
    const size = Number(item.size || item.bytes?.byteLength || 0);
    description.textContent = `${normalizedDumpFilename(item)} · ${size.toLocaleString()} bytes`;
    info.append(title, description);
    const actions = document.createElement('div');
    actions.className = 'spi-download-actions';
    const download = document.createElement('button');
    download.type = 'button';
    download.className = 'button button-quiet';
    download.textContent = 'Download';
    download.addEventListener('click', () => void downloadStoredMemoryDump(item));
    const flash = document.createElement('button');
    flash.type = 'button';
    flash.className = 'button';
    flash.textContent = item.source === 'recovery' ? 'Restore' :
      (item.memory === 'flash' ? 'Flash' : 'Write');
    flash.title = item.source === 'recovery'
      ? 'Restore the saved FLASH image to recover the device.'
      : 'Open the matching section and load this image for review before writing.';
    flash.addEventListener('click', () => void (item.source === 'recovery'
      ? restorePendingMemoryDump(item) : prepareStoredMemoryDump(item)));
    actions.append(download, flash);
    row.append(info, actions);
    container.append(row);
  }
  elements.spiDownloadsSection.hidden = visibleDownloads.length === 0;
}

async function loadMemoryDownloads() {
  let db;
  try {
    db = await openSpiRecoveryDatabase();
    const readAll = (storeName) => new Promise((resolve, reject) => {
      const request = db.transaction(storeName, 'readonly').objectStore(storeName).getAll();
      request.onsuccess = () => resolve(request.result || []);
      request.onerror = () => reject(request.error);
    });
    const readBackup = (key) => new Promise((resolve, reject) => {
      const request = db.transaction('backup', 'readonly').objectStore('backup').get(key);
      request.onsuccess = () => resolve(request.result || null);
      request.onerror = () => reject(request.error);
    });
    const filenameDate = (name) => {
      const match = /(\d{4})-(\d{2})-(\d{2})_(\d{2})-(\d{2})-(\d{2})/.exec(name || '');
      return match ? new Date(Number(match[1]), Number(match[2]) - 1, Number(match[3]),
        Number(match[4]), Number(match[5]), Number(match[6])).getTime() : 0;
    };
    const [items, legacySpi, latest, manual, recovery] = await Promise.all([
      readAll('downloads'), readAll('spi-dumps'), readBackup('latest'),
      readBackup('manual'), readBackup('spi-recovery')
    ]);
    const migrated = [];
    memoryDownloads = await Promise.all(items.map(async (item) => {
      const migratedManual = item.id === 'backup-manual' && item.source === 'manual';
      const {bytes, ...metadata} = item;
      const result = {...metadata, source: migratedManual ? 'legacy' : item.source,
        savedAt: migratedManual ? filenameDate(item.name) : item.savedAt,
        size: item.size || bytes?.byteLength || 0};
      if (result.memory === 'flash') {
        result.firmwareKind = await classifyFlashDump(bytes ? new Uint8Array(bytes) : null);
        if (result.firmwareKind !== item.firmwareKind) migrated.push({...result, bytes});
      }
      return result;
    }));
    const ids = new Set(memoryDownloads.map((item) => item.id));
    for (const [key, value] of [['latest', latest], ['manual', manual], ['spi-recovery', recovery]]) {
      if (!value?.bytes) continue;
      const recordId = `backup-${key}`;
      const source = key === 'latest' ? 'original' : key === 'manual' ? 'legacy' : 'recovery';
      const alreadyListed = ids.has(recordId) || memoryDownloads.some((item) =>
        (item.source === source || (key === 'manual' && item.source === 'manual')) &&
        item.name === value.name);
      if (!alreadyListed) {
        const record = {id: recordId, name: value.name, memory: 'flash', startAddress: 0,
          source, savedAt: value.savedAt || filenameDate(value.name), size: value.bytes.byteLength,
          firmwareKind: await classifyFlashDump(new Uint8Array(value.bytes))};
        memoryDownloads.push(record);
        migrated.push({...record, bytes: value.bytes});
      }
    }
    for (const old of legacySpi) {
      const recordId = `legacy-spi-${old.savedAt}`;
      if (ids.has(recordId)) continue;
      const match = /spi-(\d+)-/.exec(old.name || '');
      const record = {id: recordId, name: old.name || 'spi-dump.bin', memory: 'spi',
        startAddress: match ? Number(match[1]) : 0, source: 'spi', savedAt: old.savedAt || Date.now(),
        size: old.bytes?.byteLength || 0, bytes: old.bytes};
      memoryDownloads.push({...record, bytes: undefined});
      migrated.push(record);
    }
    if (migrated.length) await new Promise((resolve, reject) => {
      const tx = db.transaction('downloads', 'readwrite');
      for (const item of migrated) tx.objectStore('downloads').put(item, item.id);
      tx.oncomplete = resolve;
      tx.onerror = () => reject(tx.error || new Error('Could not index saved dumps.'));
      tx.onabort = () => reject(tx.error || new Error('Dump index update was aborted.'));
    });
    memoryDownloads.sort((a, b) => b.savedAt - a.savedAt);
    renderMemoryDownloads();
  } catch { /* Current downloads remain available in the browser's download history. */ }
  finally { if (db) db.close(); }
}

async function getStoredMemoryDump(id) {
  const cached = memoryDownloads.find((item) => item.id === id)?.volatileBytes;
  if (cached) return cached.slice();
  const db = await openSpiRecoveryDatabase();
  try {
    const entry = await new Promise((resolve, reject) => {
      const request = db.transaction('downloads', 'readonly').objectStore('downloads').get(id);
      request.onsuccess = () => resolve(request.result || null);
      request.onerror = () => reject(request.error);
    });
    if (!entry?.bytes) throw new Error('Stored dump bytes are unavailable.');
    return new Uint8Array(entry.bytes);
  } finally { db.close(); }
}

async function downloadStoredMemoryDump(item) {
  try {
    const bytes = item.bytes ? (item.bytes instanceof Uint8Array ? item.bytes : new Uint8Array(item.bytes))
      : await getStoredMemoryDump(item.id);
    downloadDump(bytes, normalizedDumpFilename(item));
  }
  catch (error) { setDumpStatus(`Could not load ${item.name}: ${error.message}`, 'error'); }
}

async function restorePendingMemoryDump(item) {
  try {
    const bytes = item.bytes ? (item.bytes instanceof Uint8Array ? item.bytes : new Uint8Array(item.bytes))
      : await getStoredMemoryDump(item.id);
    await restoreStoredFlashBackup({bytes, name: item.name},
      'Restore the pending FLASH recovery image? This replaces the temporary SPI service with the firmware captured before the operation.',
      'the pending FLASH recovery image');
  } catch (error) { setDumpStatus(`Could not restore ${item.name}: ${error.message}`, 'error'); }
}

async function prepareStoredMemoryDump(item) {
  try {
    selectPage('firmware');
    const bytes = await getStoredMemoryDump(item.id);
    const file = new File([bytes], normalizedDumpFilename(item), {type: 'application/octet-stream'});
    const input = item.memory === 'flash' ? elements.firmwareFile :
      item.memory === 'eeprom' ? elements.eepromImageFile : elements.spiImageFile;
    const transfer = new DataTransfer();
    transfer.items.add(file);
    input.files = transfer.files;
    if (item.memory === 'flash') {
      elements.firmwareTools.open = true;
      await loadFirmwareImage(file);
      elements.firmwareFile.scrollIntoView({behavior: 'smooth', block: 'center'});
    } else if (item.memory === 'eeprom') {
      document.querySelector('#eeprom-write-section').open = true;
      elements.eepromAddressFrom.value = item.startAddress || 0;
      elements.eepromAddressTo.value = (item.startAddress || 0) + (item.size || bytes.length) - 1;
      elements.eepromAddressFrom.dispatchEvent(new Event('input', {bubbles: true}));
      elements.eepromAddressTo.dispatchEvent(new Event('input', {bubbles: true}));
      await loadEepromImage();
      elements.eepromImageFile.scrollIntoView({behavior: 'smooth', block: 'center'});
    } else {
      elements.spiNorSection.open = true;
      elements.spiAddressFrom.value = item.startAddress || 0;
      elements.spiAddressTo.value = (item.startAddress || 0) + (item.size || bytes.length) - 1;
      elements.spiAddressFrom.dispatchEvent(new Event('input', {bubbles: true}));
      elements.spiAddressTo.dispatchEvent(new Event('input', {bubbles: true}));
      await loadSpiImage();
      elements.spiImageFile.scrollIntoView({behavior: 'smooth', block: 'center'});
    }
  } catch (error) { setDumpStatus(`Could not load ${item.name}: ${error.message}`, 'error'); }
}

function setSpiRecoveryBackup(bytes, name) {
  spiRecoveryBackup = bytes ? {bytes, name} : null;
  if (elements.spiRecoveryControls) elements.spiRecoveryControls.hidden = !spiRecoveryBackup;
  if (spiRecoveryBackup && elements.firmwareTools) elements.firmwareTools.open = true;
  renderMemoryDownloads();
  updateSpiWriteButton();
  updateSpiDumpAvailability();
}

function setLatestFlashBackup(bytes, name) {
  latestFlashBackup = bytes ? {bytes, name} : null;
}

function setManualFlashBackup(bytes, name) {
  manualFlashBackup = bytes ? {bytes, name} : null;
  if (elements.restoreManualFlash) elements.restoreManualFlash.hidden = !manualFlashBackup;
}

async function saveLatestFlashBackup(bytes, name) {
  const db = await openSpiRecoveryDatabase();
  try {
    await new Promise((resolve, reject) => {
      const transaction = db.transaction('backup', 'readwrite');
      transaction.objectStore('backup').put({bytes: bytes.slice().buffer, name}, 'latest');
      transaction.oncomplete = resolve;
      transaction.onerror = () => reject(transaction.error || new Error('Could not save FLASH backup.'));
      transaction.onabort = () => reject(transaction.error || new Error('FLASH backup save was aborted.'));
    });
  } finally { db.close(); }
  setLatestFlashBackup(bytes, name);
  await rememberMemoryDump(bytes, name, 'flash', 0, 'original', 'backup-latest');
}

async function saveManualFlashBackup(bytes, name) {
  const db = await openSpiRecoveryDatabase();
  try {
    await new Promise((resolve, reject) => {
      const transaction = db.transaction('backup', 'readwrite');
      transaction.objectStore('backup').put({bytes: bytes.slice().buffer, name}, 'manual');
      transaction.oncomplete = resolve;
      transaction.onerror = () => reject(transaction.error || new Error('Could not save manual FLASH dump.'));
      transaction.onabort = () => reject(transaction.error || new Error('Manual FLASH dump save was aborted.'));
    });
  } finally { db.close(); }
  setManualFlashBackup(bytes, name);
  await rememberMemoryDump(bytes, name, 'flash', 0, 'manual');
}

async function saveSpiRecoveryBackup(bytes, name) {
  const db = await openSpiRecoveryDatabase();
  try {
    await new Promise((resolve, reject) => {
      const transaction = db.transaction('backup', 'readwrite');
      transaction.objectStore('backup').put(
        {bytes: bytes.slice().buffer, name, pendingRecovery: true}, 'spi-recovery');
      transaction.oncomplete = resolve;
      transaction.onerror = () => reject(transaction.error || new Error('Could not save SPI recovery FLASH.'));
      transaction.onabort = () => reject(transaction.error || new Error('SPI recovery FLASH save was aborted.'));
    });
  } finally { db.close(); }
  setSpiRecoveryBackup(bytes, name);
}

async function loadLatestFlashBackup(strict = false) {
  let db = null;
  try {
    db = await openSpiRecoveryDatabase();
    const readEntry = async (key) => new Promise((resolve, reject) => {
      const request = db.transaction('backup', 'readonly').objectStore('backup').get(key);
      request.onsuccess = () => resolve(request.result || null);
      request.onerror = () => reject(request.error);
    });
    const item = await readEntry('latest');
    const manual = await readEntry('manual');
    const recovery = await readEntry('spi-recovery');
    if (item?.bytes?.byteLength === STM32_FLASH_SIZE) {
      const bytes = new Uint8Array(item.bytes);
      const name = item.name || 'flash-backup.bin';
      setLatestFlashBackup(bytes, name);
      // Migrate recovery records written by the earlier single-slot format.
      if (item.pendingRecovery === true && recovery?.bytes?.byteLength !== STM32_FLASH_SIZE) {
        setSpiRecoveryBackup(bytes, name);
        setDumpStatus('A pending FLASH recovery backup is available. Restore it before using the device.', 'warning');
      }
    }
    if (manual?.bytes?.byteLength === STM32_FLASH_SIZE) {
      setManualFlashBackup(new Uint8Array(manual.bytes), manual.name || 'flash-manual.bin');
    }
    if (recovery?.bytes?.byteLength === STM32_FLASH_SIZE) {
      setSpiRecoveryBackup(new Uint8Array(recovery.bytes), recovery.name || 'flash-recovery.bin');
      setDumpStatus('A pending FLASH recovery backup is available. Restore it before using the device.', 'warning');
    }
  } catch (error) {
    if (strict) throw new Error(`Could not check saved FLASH backup: ${error.message}`);
    /* In-memory recovery remains available during this page session. */
  } finally {
    if (db) db.close();
  }
}

async function ensureFlashBackup() {
  if (dumpBusy) throw new Error('Another device operation is already running.');
  const ownsLock = !firmwareBackupBusy;
  if (ownsLock) {
    firmwareBackupBusy = true;
    setDumpBusy(dumpBusy);
  }
  try {
    if (latestFlashBackup?.bytes?.length !== STM32_FLASH_SIZE) {
      await loadLatestFlashBackup(true);
    }
    if (spiRecoveryBackup) {
      throw new Error('A pending SPI recovery must be completed before starting another firmware or SPI operation.');
    }
    if (latestFlashBackup?.bytes?.length === STM32_FLASH_SIZE) {
      appendFirmwareLog(`Saved FLASH backup found (${latestFlashBackup.name}); reusing it.`);
      return true;
    }

    appendFirmwareLog('No saved FLASH backup found. Capturing one before the operation.');
    const saved = await dumpRomMemory(
      'FLASH', STM32_FLASH_BASE, STM32_FLASH_SIZE, 'flash', true, true);
    if (!saved || latestFlashBackup?.bytes?.length !== STM32_FLASH_SIZE) {
      throw new Error('The FLASH backup could not be fully read and saved. No firmware or SPI data was changed.');
    }
    appendFirmwareLog('First FLASH backup captured and saved. Continuing with the requested operation.', 'success');
    return true;
  } finally {
    if (ownsLock) {
      firmwareBackupBusy = false;
      setDumpBusy(dumpBusy);
    }
  }
}

async function clearSpiRecoveryBackup() {
  let db;
  try { db = await openSpiRecoveryDatabase(); }
  catch (error) {
    // No IndexedDB entry could have been persisted in this case; discard only
    // the in-memory temporary snapshot after the verified hardware restore.
    setSpiRecoveryBackup(null, '');
    appendFirmwareLog(`Temporary recovery snapshot cleared from memory; browser storage is unavailable (${error.message}).`, 'warning');
    return;
  }
  try {
    await new Promise((resolve, reject) => {
      const transaction = db.transaction('backup', 'readwrite');
      const store = transaction.objectStore('backup');
      const request = store.get('latest');
      request.onsuccess = () => {
        store.delete('spi-recovery');
        if (request.result?.bytes) store.put({...request.result, pendingRecovery: false}, 'latest');
      };
      transaction.oncomplete = resolve;
      transaction.onerror = () => reject(transaction.error);
    });
  } finally { db.close(); }
  setSpiRecoveryBackup(null, '');
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
    try {
      await saveSpiRecoveryBackup(backup, name);
    }
    catch (error) {
      setSpiRecoveryBackup(backup, name);
      downloadDump(backup, normalizedDumpFilename({memory: 'flash', source: 'recovery', name,
        size: backup.length, savedAt: Date.now(), firmwareKind: await classifyFlashDump(backup)}));
      appendFirmwareLog(`Persistent backup unavailable (${error.message}); requesting an external backup confirmation.`, 'warning');
      if (!window.confirm('This browser cannot retain a recovery backup after the page closes. Confirm that the downloaded 64 KB FLASH backup is saved somewhere safe before continuing.')) {
        throw new Error('SPI service was cancelled because a persistent or externally saved FLASH backup is required.');
      }
    }
    void rememberMemoryDump(backup, name, 'flash', 0, 'recovery');
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

async function restoreSpiRecoveryBackup(port, transport, showStatus = true,
  restoreDescription = 'the saved FLASH image') {
  const backup = spiRecoveryBackup;
  if (!backup?.bytes || backup.bytes.length !== STM32_FLASH_SIZE) throw new Error('No complete FLASH recovery backup is available.');
  setDumpStatus(`Restoring and verifying ${restoreDescription}вЂ¦`, 'warning');
  appendFirmwareLog(`Restoring ${restoreDescription}.`);
  const pages = validateFirmwareImage(backup.bytes, new Uint8Array(STM32_FLASH_SIZE).fill(1));
  await programFlashImage(port, pages, 'FLASH restore', true);
  let applicationOnline = true;
  try { await reconnectAndCheckFirmware(port, transport, false); }
  catch (error) {
    applicationOnline = false;
    appendFirmwareLog(`FLASH read-back verification passed, but restored firmware did not answer HELLO: ${error.message}`, 'warning');
  }
  // programFlashImage already verified every restored FLASH page. HELLO is not
  // expected from stock firmware, so it must not keep the per-operation snapshot.
  await clearSpiRecoveryBackup();
  if (applicationOnline) {
    appendFirmwareLog(`${restoreDescription} restored, read-back verified, and HELLO confirmed.`, 'success');
    if (showStatus) setDumpStatus('FLASH image restored and verified. Device is running normally.', 'success');
  } else {
    appendFirmwareLog(`${restoreDescription} restored and read-back verified. Temporary recovery image removed; saved FLASH copies remain in browser storage.`, 'success');
    if (showStatus) setDumpStatus('FLASH image restored and verified. The app did not confirm HELLO; saved browser backups remain available.', 'warning');
  }
  return applicationOnline;
}

async function runMiniSpiDump(port, transport, startAddress, length, alreadyConfirmed = false) {
  const mini = getEmbeddedSpiMiniImage();
  if (!alreadyConfirmed && !window.confirm(`This reliable SPI dump method must restart the device. The app will capture a temporary FLASH recovery copy, run the SPI service, dump offsets ${startAddress}-${startAddress + length - 1}, then restore and verify the original firmware. No permanent automatic FLASH backup is created. Continue?`)) return;
  let backup = null;
  let spiBytes = null;
  let filename = '';
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
    filename = normalizedDumpFilename({memory: 'spi', source: 'spi',
      name: makeDumpFilename(`spi-${startAddress}-${startAddress + length - 1}`),
      startAddress, size: length, savedAt: Date.now()});
    downloadDump(spiBytes, filename);
    void rememberSpiDump(spiBytes, filename, startAddress);
    appendFirmwareLog(`SPI dump available as ${filename}; starting firmware recovery.`, 'success');
  } catch (error) {
    operationError = error;
    appendFirmwareLog(`SPI service operation failed: ${error.message}`, 'error');
  }
  if (backup) {
    try { originalApplicationOnline = await restoreSpiRecoveryBackup(port, transport, false); }
    catch (error) {
      appendFirmwareLog(`CRITICAL: automatic FLASH recovery failed: ${error.message}`, 'error');
      setDumpStatus(`CRITICAL: firmware recovery failed. Keep the device connected and use Restore on the pending FLASH recovery in Downloads. ${error.message}`, 'error');
      return;
    }
  }
  if (operationError) {
    setDumpStatus(`SPI dump failed; original firmware was restored and verified. ${operationError.message}`, 'error');
    return;
  }
  appendFirmwareLog(`SPI dump saved as ${filename}; original firmware restored.`, 'success');
  setDumpStatus(originalApplicationOnline
    ? `SPI dump saved as ${filename} (${spiBytes.length.toLocaleString()} bytes). Original firmware was restored and verified.`
    : `SPI dump saved as ${filename} (${spiBytes.length.toLocaleString()} bytes). Original FLASH was restored and verified; HELLO was not confirmed, and the permanent backup remains available.`
  , originalApplicationOnline ? 'success' : 'warning');
}

async function restoreStoredFlashBackup(backup, confirmMessage, restoreDescription) {
  if (dumpBusy || firmwareFlashBusy || firmwareBackupBusy) return;
  if (!backup?.bytes) return;
  if (!window.confirm(confirmMessage)) return;
  beginFirmwareLogAction('FLASH restore');
  try {
    const {port, transport} = await selectFirmwarePort();
    setDumpBusy(true);
    await saveSpiRecoveryBackup(backup.bytes, backup.name);
    await restoreSpiRecoveryBackup(port, transport, true, restoreDescription);
  } catch (error) {
    appendFirmwareLog(`FLASH restore failed: ${error.message}`, 'error');
    setDumpStatus(`FLASH restore failed. The saved backup remains available: ${error.message}`, 'error');
  } finally { setDumpBusy(false); }
}

async function restoreManualFlashBackup() {
  await restoreStoredFlashBackup(manualFlashBackup,
    'Restore the original 64 KB FLASH image? This erases the current firmware.',
    'the original FLASH image');
}

async function dumpSpiMemory(range) {
  if (dumpBusy || firmwareFlashBusy || firmwareBackupBusy) return;
  beginFirmwareLogAction('SPI dump');
  if (spiRecoveryBackup) {
    setDumpStatus('A pending FLASH recovery backup must be restored before starting another SPI operation.', 'error');
    return;
  }
  setDumpBusy(true);
  appendFirmwareLog(`SPI dump requested at offsets ${range.start}–${range.end}.`);
  const method = elements.spiDumpMethod?.value || 'auto';
  try {
    if (method !== 'service') {
      try {
        if (!serial || (protocolCapabilities & CAP_GET_SPI_DUMP) === 0 ||
            !logInfo?.spiReady || !logInfo.spiSizeMb) {
          throw new Error('Connected firmware does not support direct SPI reads or the SPI chip is not ready.');
        }
        if (range.end >= logInfo.spiSizeMb * 1024 * 1024)
          throw new Error('Selected range exceeds the detected SPI size.');
        const bytes = await readSpiThroughProtocol(serial, range.length, range.start);
        const filename = normalizedDumpFilename({memory: 'spi', source: 'spi',
          name: makeDumpFilename(`spi-${range.start}-${range.end}`),
          startAddress: range.start, size: bytes.length, savedAt: Date.now()});
        downloadDump(bytes, filename);
        void rememberSpiDump(bytes, filename, range.start);
        appendFirmwareLog(`Direct SPI dump saved as ${filename}.`, 'success');
        setDumpStatus(`SPI dump saved as ${filename} (${bytes.length.toLocaleString()} bytes). The running firmware was not restarted.`, 'success');
        return;
      } catch (directError) {
        appendFirmwareLog(`Direct SPI read failed: ${directError.message}`, 'warning');
        if (method === 'direct' || !window.confirm(`The no-restart SPI dump failed: ${directError.message}\n\nTry the reliable SPI service method? This will restart the device temporarily, save a recovery copy, and restore the original firmware afterward.`)) {
          throw directError;
        }
      }
    } else if (!window.confirm('Use the reliable SPI service method? This restarts the device temporarily, saves a recovery copy, and restores the original firmware afterward.')) {
      return;
    }
    const {port, transport} = await selectFirmwarePort();
    await runMiniSpiDump(port, transport, range.start, range.length, true);
  } catch (error) {
    appendFirmwareLog(`SPI dump failed: ${error.message}`, 'error');
    setDumpStatus(`SPI dump failed: ${error.message}`, 'error');
  } finally { setDumpBusy(false); }
}

async function writeSpiImage(imageOverride = null, operationLabel = 'Write', isErase = false) {
  if (dumpBusy || firmwareFlashBusy || firmwareBackupBusy || (!selectedSpiImage && !imageOverride)) return;
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
    `${operationLabel} ${image.bytes.length.toLocaleString()} SPI NOR bytes from address ${targetStart} through ${targetEnd - 1}? Affected sectors are erased and rewritten; bytes outside the range will be preserved. This requires a temporary device restart and recovery copy. No permanent automatic FLASH backup will be created. Keep the device connected until the operation finishes.`)) return;
  beginFirmwareLogAction(`SPI ${operationLabel.toLowerCase()}`);

  if (dumpBusy || firmwareFlashBusy) return;

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
      elements.spiWriteStatus.textContent = `CRITICAL: device firmware recovery failed. Keep it connected and use Restore on the pending FLASH recovery in Downloads. ${error.message}`;
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

async function flashSelectedFirmware(actionAlreadyStarted = false) {
  if (dumpBusy || firmwareBackupBusy) return;
  if (firmwareFlashBusy) {
    firmwareStopRequested = true;
    elements.flashFirmware.disabled = true;
    elements.flashFirmware.textContent = 'Stopping…';
    appendFirmwareLog('Stop requested; waiting for the current ROM operation to finish.');
    return;
  }
  if (!selectedFirmwareImage) return;
  if (!('serial' in navigator) && !androidUsbAvailable && activeTransport !== 'webusb') {
    if (!actionAlreadyStarted) beginFirmwareLogAction('Firmware flash');
    appendFirmwareLog('USB serial access is unavailable in this browser.', 'error');
    return;
  }
  if (!actionAlreadyStarted) beginFirmwareLogAction('Firmware flash');
  const imageToFlash = selectedFirmwareImage;
  if (!window.confirm(`Flash ${imageToFlash.name}? The current device firmware will be replaced. The app will reuse the saved permanent FLASH backup or capture one before programming.`)) return;
  try {
    await ensureFlashBackup();
  } catch (error) {
    appendFirmwareLog(`Firmware update cancelled before erase: ${error.message}`, 'error');
    setRebootStatus(`Firmware update cancelled: ${error.message}`, 'error');
    return;
  }
  if (selectedFirmwareImage !== imageToFlash) {
    appendFirmwareLog('Firmware selection changed while the FLASH backup was being captured. Select Flash again.', 'warning');
    return;
  }
  firmwareFlashBusy = true;
  updateStockFirmwareButtons();
  updateSpiWriteButton();
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
    updateStockFirmwareButtons();
    updateSpiWriteButton();
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
  rateHistoryLevels.length = 0;
  rateHistoryPulses.length = 0;
  rateHistoryTimes.length = 0;
  previousRateCount = null;
  logInfo = null;
  logRecords = [];
  logRefreshRetryAt = 0;
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
      logRefreshRetryAt = 0;
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
    updateLogActions();
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
    logRefreshRetryAt = 0;
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
  logRefreshRetryAt = 0;
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
  if (activeSerial && elements.deviceScreenAlwaysOn?.dataset.enabled === 'true') {
    try {
      await activeSerial.request(OP_DEVICE_ACTION,
        Uint8Array.of(ACTION_SET_SCREEN_ALWAYS_ON, 0), 700);
    } catch { /* Firmware watchdog also returns the screen to Auto. */ }
  }
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
  beginFirmwareLogAction('Start firmware');
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
  beginFirmwareLogAction('Enter bootloader');
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

elements.deviceWakeScreen?.addEventListener('click', () =>
  runDeviceControlAction(ACTION_WAKE_SCREEN, 0, elements.deviceWakeScreen,
    'Screen wake requested'));
elements.deviceModeButtons.forEach((button) => {
  button.addEventListener('click', () => {
    const mode = Number(button.dataset.mode);
    runDeviceControlAction(ACTION_SET_INDICATION_MODE, mode, button,
      `${button.textContent} mode selected`).then((ok) => {
        if (ok) setDeviceModeActive(mode);
      });
  });
});
elements.deviceFlashlight?.addEventListener('click', () => {
  const enabled = elements.deviceFlashlight.dataset.enabled !== 'true';
  runDeviceControlAction(ACTION_SET_FLASHLIGHT, enabled ? 1 : 0,
    elements.deviceFlashlight, `Flashlight ${enabled ? 'on' : 'off'}`).then((ok) => {
      if (!ok) return;
      elements.deviceFlashlight.dataset.enabled = String(enabled);
      elements.deviceFlashlight.textContent = `Flashlight: ${enabled ? 'On' : 'Off'}`;
    });
});
elements.deviceScreenAlwaysOn?.addEventListener('click', () => {
  const enabled = elements.deviceScreenAlwaysOn.dataset.enabled !== 'true';
  runDeviceControlAction(ACTION_SET_SCREEN_ALWAYS_ON, enabled ? 1 : 0,
    elements.deviceScreenAlwaysOn,
    `Screen always on ${enabled ? 'enabled' : 'disabled'}`).then((ok) => {
      if (!ok) return;
      elements.deviceScreenAlwaysOn.dataset.enabled = String(enabled);
      elements.deviceScreenAlwaysOn.textContent = `Screen: ${enabled ? 'On' : 'Auto'}`;
    });
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
elements.rateChart.addEventListener('pointermove', (event) => {
  if (!elements.rateChartActionHint.hidden) positionRateChartActionHint(event);
});
elements.rateChart.addEventListener('pointerleave', () => {
  if (rateChartDragging) return;
  hideRateChartTooltip();
  drawRateChart();
});
elements.rateChart.addEventListener('pointerdown', (event) => {
  rateChartDragging = true;
  rateChartDragStartX = event.clientX;
  rateChartDragStartScroll = rateChartScrollSeconds;
  elements.rateChart.setPointerCapture?.(event.pointerId);
  hideRateChartTooltip();
});
elements.rateChart.addEventListener('pointermove', (event) => {
  if (!rateChartDragging) return;
  const rect = elements.rateChart.getBoundingClientRect();
  // The plotted content follows the pointer: dragging right reveals older
  // samples and moves the graph itself to the right.
  rateChartScrollSeconds = rateChartDragStartScroll +
    (event.clientX - rateChartDragStartX) * rateChartZoomSeconds / Math.max(1, rect.width);
  clampRateChartViewport();
  drawRateChart();
});
elements.rateChart.addEventListener('pointerup', (event) => {
  rateChartDragging = false;
  elements.rateChart.releasePointerCapture?.(event.pointerId);
});
elements.rateChart.addEventListener('pointercancel', () => { rateChartDragging = false; });
elements.rateChart.addEventListener('wheel', (event) => {
  event.preventDefault();
  const rect = elements.rateChart.getBoundingClientRect();
  const ratio = Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width));
  const previousZoom = rateChartZoomSeconds;
  const previousScroll = rateChartScrollSeconds;
  const ageAtPointer = rateChartScrollSeconds + (1 - ratio) * rateChartZoomSeconds;
  // Keep the usual wheel convention: wheel up zooms in, wheel down zooms out.
  // The sample below the pointer stays fixed while the viewport has room;
  // clamping only takes effect at either edge.
  rateChartZoomSeconds *= event.deltaY < 0 ? 0.8 : 1.25;
  rateChartScrollSeconds = ageAtPointer - (1 - ratio) * rateChartZoomSeconds;
  clampRateChartViewport();
  const scrollDelta = rateChartScrollSeconds - previousScroll;
  if (Math.abs(rateChartZoomSeconds - previousZoom) >= 0.01) {
    showRateChartActionHint(event.deltaY < 0 ? 'zoom-in' : 'zoom-out',
      event.deltaY < 0 ? 'Zoom in' : 'Zoom out', event);
  } else if (Math.abs(scrollDelta) >= 0.01) {
    showRateChartActionHint(scrollDelta > 0 ? 'older' : 'newer',
      scrollDelta > 0 ? 'Browse older history' : 'Browse newer history', event);
  }
  hideRateChartTooltip();
  drawRateChart();
}, {passive: false});
elements.rateChart.addEventListener('dblclick', resetRateChartViewport);
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
updateDeviceControls();
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
