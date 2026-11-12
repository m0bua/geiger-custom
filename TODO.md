# TODO

## Investigate

- [ ] **Overview rate graph drops:** occasional abrupt downward steps followed by a plateau. The rate is based on a rolling 60-second window, but compare each step with the per-second pulse history to confirm that expiring burst samples explain it. Distinguish this from isolated `GET_LOG_PAGE` timeouts; `GET_STATUS` continued during the captured timeout.
- [ ] **Battery percentage jumps:** percentage can rise while the battery is discharging, or otherwise jump unexpectedly. Compare raw ADC samples, voltage conversion, filtering, and displayed percentage over time.
- [ ] **Minute log ordering:** rows can appear out of chronological order while sorted by DATE descending (observed `17:32`, `17:33`, `17:34`, then `17:31`). Check live-record replacement, timestamps, and sorting when new records arrive.
