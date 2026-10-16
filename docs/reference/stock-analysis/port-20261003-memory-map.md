# Geiger-2 current firmware byte map

Base: `Port-4.1.17-rgb-settings-central-labels-no-cache.hex` — 65,536 bytes.

## Strictly identified free space

| Range | Bytes | Evidence | Status |
|---|---:|---|---|
| `0x0800FD6E–0x0800FD8F` | 34 | Branch at `0x0800FD6C` bypasses it; isolated probe booted. | Proven |
| `0x0800FF74–0x0800FF7F` | 12 | Padding after referenced literal `0x0800FE90`; no direct branch, pointer or PC-relative load uses the gap. | Test next |

Total proven: **34 bytes**. Additional strict candidate: **12 bytes**.

## Reserved slots, not counted as free

| Range | Bytes | Reason |
|---|---:|---|
| `0x0800FFD4–0x0800FFD7` | 4 | Alignment NOPs in the central-label entry; retain until the routine is rearranged. |
| `0x0800FFFC–0x0800FFFF` | 4 | Out-of-range sentinel entries in the central-label map. |

## Whole-image classification

| Class | Bytes | Meaning |
|---|---:|---|
| reachable code | 58874 | reachable code under conservative Thumb walk |
| literal-pool data | 5056 | word loaded through PC-relative LDR |
| allocated or unknown | 1019 | conservatively retained |
| vector table | 192 | startup/exception vectors |
| active custom renderer | 160 | current renderer code |
| active custom table | 96 | current descriptor table |
| proven free | 34 | verified bypassed gap |
| active custom hook | 26 | current labels hook |
| active custom entry | 20 | current central entry |
| active custom helper | 20 | current helper routine |
| strict candidate | 12 | candidate pad |
| active custom literals | 8 | central routine literals |
| active custom map | 8 | current map |
| reserved alignment | 4 | alignment pad retained for layout |
| reserved sentinel | 4 | map guard |
| addressed data/code | 3 | address in flash referenced by a firmware word |

`port-20261003-memory-map.csv` is exhaustive: every byte belongs to exactly one compressed interval.
## Page accounting
Every byte is assigned once in the CSV. Page totals make it possible to audit that assignment without reading 1,752 rows. `Code` is deliberately conservative: a byte is retained whenever static control flow or a possible 32-bit flash reference reaches it.

| Flash range | Vector | Code | Literals | Other allocated | Custom | Proven free | Candidate | Reserved | Total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `0x08000000–0x08000FFF` | 192 | 3622 | 44 | 238 | 0 | 0 | 0 | 0 | 4096 |
| `0x08001000–0x08001FFF` | 0 | 3854 | 96 | 146 | 0 | 0 | 0 | 0 | 4096 |
| `0x08002000–0x08002FFF` | 0 | 3866 | 196 | 34 | 0 | 0 | 0 | 0 | 4096 |
| `0x08003000–0x08003FFF` | 0 | 3568 | 512 | 16 | 0 | 0 | 0 | 0 | 4096 |
| `0x08004000–0x08004FFF` | 0 | 3654 | 408 | 34 | 0 | 0 | 0 | 0 | 4096 |
| `0x08005000–0x08005FFF` | 0 | 3764 | 300 | 32 | 0 | 0 | 0 | 0 | 4096 |
| `0x08006000–0x08006FFF` | 0 | 3896 | 176 | 24 | 0 | 0 | 0 | 0 | 4096 |
| `0x08007000–0x08007FFF` | 0 | 3930 | 140 | 26 | 0 | 0 | 0 | 0 | 4096 |
| `0x08008000–0x08008FFF` | 0 | 3858 | 212 | 26 | 0 | 0 | 0 | 0 | 4096 |
| `0x08009000–0x08009FFF` | 0 | 3488 | 576 | 32 | 0 | 0 | 0 | 0 | 4096 |
| `0x0800A000–0x0800AFFF` | 0 | 3536 | 524 | 36 | 0 | 0 | 0 | 0 | 4096 |
| `0x0800B000–0x0800BFFF` | 0 | 3630 | 436 | 30 | 0 | 0 | 0 | 0 | 4096 |
| `0x0800C000–0x0800CFFF` | 0 | 3484 | 568 | 44 | 0 | 0 | 0 | 0 | 4096 |
| `0x0800D000–0x0800DFFF` | 0 | 3424 | 668 | 4 | 0 | 0 | 0 | 0 | 4096 |
| `0x0800E000–0x0800EFFF` | 0 | 3714 | 192 | 190 | 0 | 0 | 0 | 0 | 4096 |
| `0x0800F000–0x0800FFFF` | 0 | 3586 | 8 | 110 | 338 | 34 | 12 | 8 | 4096 |

`Other allocated` includes bytes that are not instruction starts or recognized literal pools. They remain occupied until a concrete reader is identified or ruled out.
