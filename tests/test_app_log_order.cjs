/* Run with Node.js: node tests/test_app_log_order.cjs */
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');

// Model the DOM operations used by the row renderer, including moving an
// existing node (append/insertBefore must never duplicate it).
class Element {
  constructor(tag = 'div') {
    this.tagName = tag.toUpperCase();
    this.children = [];
    this.parentElement = null;
    this.dataset = {};
    this.className = '';
    this.style = {setProperty() {}};
    this.classList = {contains: (name) => this.className.split(' ').includes(name)};
  }
  get isConnected() { return this.root || Boolean(this.parentElement?.isConnected); }
  get nextElementSibling() {
    return this.parentElement?.children[this.parentElement.children.indexOf(this) + 1] ?? null;
  }
  get lastElementChild() { return this.children.at(-1); }
  remove() {
    if (this.parentElement) this.parentElement.children.splice(this.parentElement.children.indexOf(this), 1);
    this.parentElement = null;
  }
  append(node) { node.remove(); this.children.push(node); node.parentElement = this; }
  insertBefore(node, reference) {
    if (node === reference) return;
    if (!reference) return this.append(node);
    node.remove();
    const index = this.children.indexOf(reference);
    assert.ok(index >= 0, 'reference must belong to parent');
    this.children.splice(index, 0, node);
    node.parentElement = this;
  }
  replaceChildren() { for (const child of [...this.children]) child.remove(); }
  querySelectorAll(selector) {
    return this.children.filter((child) => selector === '.log-grid-header' ?
      child.classList.contains('log-grid-header') :
      child.classList.contains('log-record') && !child.classList.contains('log-total'));
  }
  querySelector(selector) { return this.querySelectorAll(selector)[0] ?? null; }
}

async function main() {
  const source = fs.readFileSync(path.join(__dirname, '../app/app.js'), 'utf8');
  new vm.Script(source, {filename: 'app/app.js'});
  const content = new Element();
  content.root = true;
  const context = vm.createContext({
    elements: {logContent: content, logPageSize: {value: 'all'},
      loadMoreLogs: {}, logsStatus: {}, logsPage: {hidden: false}},
    document: {createElement: (tag) => new Element(tag)},
  });
  const run = (code) => vm.runInContext(code, context);
  run(`
    let logRecords = [], logSortKey = 'date', logSortDirection = 'desc', logDisplayPage = 0;
    const selectedLogType = 'minutes', logTypes = [{id:'minutes', countKey:'minutes'}];
    const serial = {}, logInfo = {minutes: 200};
    let logPageLoading = false, logCsvBusy = false, logRefreshBusy = false;
    let logRefreshRetryAt = 0, logPageGeneration = 0;
    const logRangeAll = false, logRangeActive = false, logCountLimit = 0, logFillMetric = 'dose';
    var responseRecords = [];
    var responsePages = [], pageRequests = [];
    const LOG_LOAD_MORE_COUNT = 10;
    function updateLogRangeControls() {}
    function updateLogPagination() {}
    function updateLogFillOptions() {}
    function canLoadMoreLogRecords() { return true; }
    function canLoadOlderLogRange() { return false; }
    function hasCurrentLogDate() { return true; }
    function logRecordInRange() { return true; }
    function saveLogView() {}
    function logDiagnostic() {}
    async function queryLogPage(type, cursor) {
      pageRequests.push({...cursor});
      return responsePages.length ? responsePages.shift() : {records:responseRecords, more:false, cursor:{}};
    }
    function buildLogHeader() {
      const header = document.createElement('div'); header.className = 'log-grid-header'; return header;
    }
    function renderLogTotal() {
      if (elements.logContent.children.some(row => row.classList.contains('log-total'))) return;
      const total = document.createElement('div'); total.className = 'log-record log-total';
      elements.logContent.append(total);
    }
    function formatGroupedNumber(value) { return String(value); }
    function logFillValue(record) { return record.dose; }
    function formatLogDate(value) { return String(value); }
    function logValueCells(record) { return [String(record.dose)]; }
    function logDoseClass() { return ''; }
    function logRateClass() { return ''; }
    function logCoverageClass() { return ''; }
    function logCoveragePercent(record) { return record.covered; }
  `);
  for (const name of ['logRecordKey', 'logSortValue', 'sortedLogRecords', 'visibleLogRecords',
    'renderLogSummary', 'refreshLatestLogRecord', 'loadNewerLogRecords']) {
    const start = source.search(new RegExp(`^(?:async )?function ${name}\\(`, 'm'));
    assert.ok(start >= 0, name);
    const rest = source.slice(start);
    const next = rest.slice(1).search(/^(?:async )?function /m);
    run(next < 0 ? rest : rest.slice(0, next + 1));
  }
  const rows = () => content.querySelectorAll('.log-record:not(.log-total)');
  const times = () => rows().map((row) => Number(row.children[0].textContent));
  const record = (minute, dose = 1, live = false) =>
    ({timestamp: minute * 60, epoch: 4, dose, live, covered: 60});
  context.initial = [record(31, 1, true), record(30)];
  run('logRecords = initial; renderLogSummary()');
  const retained = rows()[0];
  // A response can contain several newer minutes followed by the completed
  // version of the formerly live minute. DATE descending must remain intact.
  context.responseRecords = [record(34, 4, true), record(33, 3), record(32, 2), record(31, 5)];
  await run('refreshLatestLogRecord()');
  assert.deepEqual(times(), [34, 33, 32, 31, 30].map((minute) => minute * 60));
  assert.equal(rows()[3], retained, 'reuse the former live row');
  assert.equal(retained.children[1].textContent, '5', 'update completed live record');
  context.responseRecords = [record(34, 9, true)];
  const liveRow = rows()[0];
  await run('refreshLatestLogRecord()');
  assert.equal(rows()[0], liveRow);
  assert.equal(liveRow.children[1].textContent, '9');
  assert.equal(rows().length, 5, 'live refresh must not duplicate rows');

  // Historical batches also use preserved rows; insert by date even if a
  // response interleaves records with those already displayed.
  context.extra = record(32.5);
  run('logRecords.push(extra); renderLogSummary(true)');
  assert.deepEqual(times(), [34, 33, 32.5, 32, 31, 30].map((minute) => minute * 60));

  context.many = Array.from({length: 101}, (_, i) => record(200 - i));
  run("logRecords = many; elements.logPageSize.value = '10'; logDisplayPage = 1; renderLogSummary()");
  assert.deepEqual(times(), Array.from({length: 10}, (_, i) => (190 - i) * 60));
  context.newest = record(201);
  run('logRecords.unshift(newest); renderLogSummary(true)');
  assert.deepEqual(times(), Array.from({length: 10}, (_, i) => (191 - i) * 60));
  assert.equal(new Set(rows().map((row) => row.dataset.logKey)).size, 10);
  assert.ok(content.children[0].classList.contains('log-grid-header'));
  assert.ok(content.children[1].classList.contains('log-total'));
  run("logSortDirection = 'asc'; logDisplayPage = 0; renderLogSummary(true)");
  assert.deepEqual(times(), Array.from({length: 10}, (_, i) => (100 + i) * 60));
  run("logSortKey = 'dose'; logSortDirection = 'desc'; elements.logPageSize.value = 'all'; logRecords = initial.concat(responseRecords); renderLogSummary(true)");
  assert.equal(rows()[0].children[1].textContent, '9');

  // Firmware may return only one record per page. Continue past the new
  // minute(s) until the previous live minute has been reread from the device.
  for (const newerCount of [1, 3, 12]) {
    context.initial = [record(31, 1, true), record(30)];
    context.responsePages = Array.from({length: newerCount + 1}, (_, i) => {
      const minute = 31 + newerCount - i;
      return {records: [{...record(minute, i === newerCount ? 99 : 2, i === 0),
        average: 42, maximum: 80, partial: false}],
        more: true, cursor: {time: minute * 60, epoch: 4, skip: 0}};
    });
    run("logRecords = initial; logSortKey = 'date'; logSortDirection = 'desc'; pageRequests = []; renderLogSummary()");
    await run('refreshLatestLogRecord()');
    assert.equal(run('pageRequests.length'), newerCount + 1, 'reread previous minute across page/batch boundaries');
    assert.equal(run('logRecords.find(record => record.timestamp === 31 * 60).dose'), 99);
    assert.equal(run('logRecords.find(record => record.timestamp === 31 * 60).live'), false);
    assert.equal(run('logRecords.find(record => record.timestamp === 31 * 60).average'), 42);
    assert.equal(run('logRecords.find(record => record.timestamp === 31 * 60).maximum'), 80);
    assert.equal(rows()[newerCount].children[1].textContent, '99');
    assert.deepEqual(times(), Array.from({length: newerCount + 2}, (_, i) => (31 + newerCount - i) * 60));
    for (let i = 1; i <= newerCount; ++i) {
      assert.equal(run(`pageRequests[${i}].time`), (32 + newerCount - i) * 60, 'continue using device cursor');
    }
  }
  console.log('PASS: live finalization across pages/batches, row order, pagination and sorting');
}

module.exports = main;
if (require.main === module) main().catch((error) => { console.error(error); process.exitCode = 1; });
