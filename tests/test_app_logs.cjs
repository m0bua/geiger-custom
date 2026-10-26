/* Run with Node.js: node tests/test_app_logs.cjs */
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');

async function main() {
  const app = fs.readFileSync(path.join(__dirname, '../app/app.js'), 'utf8');
  new vm.Script(app, {filename: 'app/app.js'});
  class Element {
    constructor() { this.children = []; this.dataset = {}; this.hidden = false; this.value = ''; this.textContent = ''; this.listeners = {}; this.style = {setProperty() {}}; this.classList = {toggle() {}}; }
    append(...nodes) { this.children.push(...nodes); }
    replaceChildren() { this.children = []; }
    addEventListener(event, handler) { this.listeners[event] = handler; }
    setAttribute() {}
    querySelectorAll() { return this.children.filter((item) => item.dataset.logType); }
  }
  const elements = Object.fromEntries(['logContent', 'logDate', 'loadLogDate', 'loadMoreLogs', 'logsStatus', 'logTabs', 'logsPage'].map((name) => [name, new Element()]));
  const debug = {};
  const start = (Date.parse('2026-10-22T12:00:00Z') - Date.UTC(2000, 0, 1)) / 1000;
  const data = [[], [], [], []];
  for (let i = 0; i < 3200; ++i) data[0].push({time: start - i * 60, count: i + 1, avg: 17 + i, max: 99 + i, covered: 60, epoch: 4, flags: 9, id: 0});
  for (let i = 0; i < 60; ++i) data[1].push({time: start - i * 3600, count: 3600, avg: 60, max: 120, covered: 3600, epoch: 4, flags: 9, id: 0});
  for (let i = 0; i < 60; ++i) data[2].push({time: start - i * 86400, count: 86400, avg: 60, max: 120, covered: 86400, epoch: 4, flags: 9, id: 0});
  const requests = [];
  let malformed = false;
  const serial = {async request(opcode, request) {
    if (opcode === 5) {
      const payload = new Uint8Array(30), view = new DataView(payload.buffer);
      for (let i = 0; i < 4; ++i) view.setUint16(2 + i * 2, data[i].length, true);
      payload[10] = 1; view.setUint32(11, start, true); payload[26] = 2; payload[29] = 90;
      return payload;
    }
    assert.equal(opcode, 6); assert.equal(request.length, 18);
    const input = new DataView(request.buffer, request.byteOffset, request.byteLength);
    const type = request[0], count = request[1], period = input.getUint32(2, true);
    assert.ok(count === 1 || count === 2);
    const cursorTime = input.getUint32(6, true), cursorEpoch = input.getUint16(10, true), limit = input.getUint32(14, true);
    requests.push({type, count, period, limit});
    if (malformed) { malformed = false; return Uint8Array.of(0); }
    const available = data[type].filter((item) => (item.epoch < cursorEpoch || item.epoch === cursorEpoch && item.time < cursorTime) && (!limit || item.time <= limit));
    const rows = available.slice(0, count);
    const payload = new Uint8Array(12 + rows.length * 25), output = new DataView(payload.buffer);
    payload[1] = type; payload[2] = rows.length; payload[3] = available.length > rows.length ? 1 : 0;
    const last = rows.at(-1);
    output.setUint32(4, last?.time ?? cursorTime, true); output.setUint16(8, last?.epoch ?? cursorEpoch, true);
    rows.forEach((row, index) => {
      const at = 12 + index * 25;
      for (const [offset, key] of [[0, 'time'], [4, 'count'], [8, 'avg'], [12, 'max'], [16, 'covered']]) output.setUint32(at + offset, row[key], true);
      output.setUint16(at + 20, row.epoch, true); payload[at + 22] = row.flags; output.setUint16(at + 23, row.id, true);
    });
    return payload;
  }};
  const context = vm.createContext({elements, serial, document: {createElement() { return new Element(); }, querySelector(selector) { return debug[selector] ??= new Element(); }},
    console, Uint8Array, DataView, Date, Map, Set, Math, ProtocolError: Error,
    renderDebugLogInfo() {}, updateDeviceDateTime() {}, updateSpiDumpAvailability() {},
    formatRadiation(value, prefix, symbol, rate) { return {value: value.toFixed(3), unit: `${symbol}${rate ? '/h' : ''}`}; }});
  const prefix = `
    const OP_GET_LOG_INFO=5, OP_GET_LOG_PAGE=6, LOG_BATCH_SIZE=5;
    const LOG_INITIAL_TARGETS={minutes:60,hours:24,days:30,alerts:5};
    let logInfo=null,selectedLogType='minutes',logRecords=[],logCursor={time:0xffffffff,epoch:0xffff,skip:0};
    let logHasMore=true,logHistorical=false,logSelectedDate='',logStartOffset=0,logPageTotal=0;
    let logPageLoading=false,logPageReloadPending=false,logPageGeneration=0,logRefreshBusy=false,logInfoLoading=false,logCountsRefreshBusy=false;
    const logViewCache=new Map(); let deviceSettings=null;
  `;
  const section = app.slice(app.indexOf('function applyLogInfo('), app.indexOf('async function synchronizeDeviceClock('));
  vm.runInContext(prefix + section, context);
  const run = (code) => vm.runInContext(code, context);
  await run('serial.request(5).then(applyLogInfo)'); run('renderLogTabs()');
  await run('loadLogPage(false)');
  assert.equal(run('logRecords.length'), 60);
  assert.deepEqual(requests.slice(0, 6).map((item) => item.count), [2, 2, 1, 2, 2, 1]);
  assert.equal(run('logRecords[0].average'), 17);
  assert.equal(run('logRecords[0].maximum'), 99);
  assert.ok(!elements.logContent.children[0].innerHTML.includes('Minimum'));
  await run('loadLogPage(false)'); assert.equal(run('logRecords.length'), 120);
  run("saveLogView(); ++logPageGeneration; selectedLogType='hours'; clearLogView();");
  await run('loadLogPage(false)'); assert.equal(run('logRecords.length'), 24);
  run("saveLogView(); ++logPageGeneration; selectedLogType='minutes'; restoreLogView();");
  assert.equal(run('logRecords.length'), 120);
  data[0].unshift({time: start + 60, count: 6, avg: 60, max: 90, covered: 6, epoch: 4, flags: 15, id: 0});
  await run('refreshLatestLogRecord()');
  assert.equal(run('logRecords.length'), 121); assert.equal(run('logRecords[0].dose'), 6);
  data[0][0] = {...data[0][0], count: 12, covered: 12};
  await run('refreshLatestLogRecord()');
  assert.equal(run('logRecords.length'), 121); assert.equal(run('logRecords[0].dose'), 12);
  for (let i = 1; i <= 7; ++i) data[0].unshift({time: start + 60 + i * 60, count: 60, avg: 60, max: 60, covered: 60, epoch: 4, flags: 9, id: 0});
  await run('loadNewerLogRecords()'); assert.equal(run('logRecords.length'), 128);
  assert.equal(run('new Set(logRecords.map(logRecordKey)).size'), 128);
  elements.logDate.value = '2026-10-21'; await run('loadLogNearDate()');
  assert.equal(run('logRecords.length'), 60);
  assert.equal(run('logRecords[0].timestamp'), start - 12 * 3600 - 60);
  const before = requests.length; await run('refreshLatestLogRecord()'); assert.equal(requests.length, before);
  elements.logDate.value = '2026-10-22'; await run('loadLogNearDate()');
  assert.ok(run('logRecords.length') >= 128);
  malformed = true;
  const retained = run('logRecords.length'); await run('loadLogPage(false)');
  assert.equal(run('logRecords.length'), retained);
  assert.match(elements.logsStatus.textContent, /Invalid canonical/);
  await run('refreshLogCounts()');
  assert.equal(debug['#debug-minutes'].textContent, data[0].length.toLocaleString('en-US'));
  console.log('PASS: app parses canonical CNT/AVG/MAX, batches 5 toward 60/24/30, repeats full pages, caches tabs/dates, refreshes live rows without duplicates, and rejects malformed pages');
}

module.exports = main;
if (require.main === module) main().catch((error) => { console.error(error); process.exitCode = 1; });
