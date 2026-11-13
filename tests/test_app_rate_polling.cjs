/* Run with Node.js: node tests/test_app_rate_polling.cjs */
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');

function main() {
  const source = fs.readFileSync(path.join(__dirname, '../app/app.js'), 'utf8');
  new vm.Script(source, {filename: 'app/app.js'});
  let callback, requests = 0, starts = 0, stops = 0;
  const context = vm.createContext({
    elements: {deviceModeButtons: [{dataset: {}}]},
    setInterval(fn, delay) { assert.equal(delay, 100); callback = fn; starts++; return 1; },
    clearInterval(id) { assert.equal(id, 1); stops++; },
    refreshStatus(quiet) { assert.equal(quiet, true); requests++; },
  });
  const run = (code) => vm.runInContext(code, context);
  run(`let serial = {}, pollTimer = 1, fastRatePollTimer = 0, deviceIndicationMode = null;
    const rateHistory = [], rateHistoryLevels = [], rateHistoryPulses = [], rateHistoryTimes = [];
    let rateHistorySampleSecond = null;
    const RATE_CHART_BUFFER_SECONDS = 3;`);
  for (const name of ['setDeviceModeActive', 'recordRateHistory']) {
    const start = source.indexOf(`function ${name}(`);
    assert.ok(start >= 0);
    const rest = source.slice(start);
    const next = rest.slice(1).search(/^(?:async )?function /m);
    run(rest.slice(0, next + 1));
  }
  for (const mode of [null, 0, 1, 2, 1]) {
    run(`setDeviceModeActive(${mode})`);
    callback();
  }
  assert.equal(starts, 1, 'USB polls at 100 ms in every mode without duplicate timers');
  assert.equal(requests, 5);
  run('pollTimer = 0'); callback();
  assert.equal(requests, 5, 'clock synchronization pauses fast polling');
  run('serial = null'); callback();
  assert.equal(stops, 1);
  assert.equal(run('fastRatePollTimer'), 0);

  for (let second = 0; second < 3; ++second) {
    for (let tick = 0; tick < 10; ++tick) {
      const appended = run(`recordRateHistory(${second * 10 + tick}, 'green', 1, ${1000 + second}, ${second * 1000 + tick * 100})`);
      assert.equal(appended, tick === 0, 'only a new second advances historical scrolling');
    }
  }
  assert.equal(run('rateHistory.length'), 3, '10-Hz polling preserves chart duration');
  assert.equal(run('rateHistory.at(-1)'), 29, 'newest sample stays live');
  assert.equal(run('rateHistoryPulses.reduce((a, b) => a + b, 0)'), 30, 'retain every pulse');
  run("recordRateHistory(30, 'yellow', 2, 1003, 3000)");
  assert.equal(run('rateHistory.length'), 3);
  assert.equal(run('rateHistory[0]'), 19, 'trim all history arrays together');
  assert.equal(run('rateHistoryTimes[0]'), 1001);
  assert.equal(run('rateHistoryPulses[0]'), 10);
  assert.equal(run('rateHistoryLevels.at(-1)'), 'yellow');
  run('rateHistory.length = rateHistoryLevels.length = rateHistoryPulses.length = rateHistoryTimes.length = 0');
  assert.equal(run("recordRateHistory(1, 'blue', 0, 2000, 3050)"), true, 'reset starts a bucket in the same second');
  console.log('PASS: USB polling, mode transitions, pause/disconnect, one-second history and pulse totals');
}

module.exports = main;
if (require.main === module) main();
