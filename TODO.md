# TODO

## Investigate

- [ ] **Overview rate graph drops:** occasional abrupt downward steps followed by a plateau. The rate is based on a rolling 60-second window, but compare each step with the per-second pulse history to confirm that expiring burst samples explain it. Distinguish this from isolated `GET_LOG_PAGE` timeouts; `GET_STATUS` continued during the captured timeout.
- [ ] **Battery percentage jumps:** percentage can rise while the battery is discharging, or otherwise jump unexpectedly. Compare raw ADC samples, voltage conversion, filtering, and displayed percentage over time.
- [x] **Minute log ordering:** fixed reversed insertion of multiple new rows during live refresh. Reused and new rows now follow the selected sort order, including across pagination. Live refresh continues through device pages to reread the previous live period and replace it with final device values. Regression coverage: `node tests/test_app_log_order.cjs` (live-record finalization across pages/batches, multiple new minutes, historical batches, pagination, and sorting).
