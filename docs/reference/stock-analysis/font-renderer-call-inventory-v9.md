# Mira Geiger-2: small-font renderer inventory (v9 baseline)

Baseline: `port-20261012-small-to-tiny-unified-map-v9.hex`  
SHA-256: `bfd53e663746fec925c69841ae983ca7d470cc6596821510697199f506135f63`

## What the scan proves

A Thumb BL-target scan finds **57 call sites** to the common low-level text routine at `0x08008E48`. This establishes that the firmware already has a common glyph-drawing core. It does **not** establish a single shared small-font rendering path: callers pass different strings, positions, dimensions, descriptors or runtime state. Unifying only the glyph table therefore does not satisfy the requested change.

The four call sites already identified as former-Small live paths are:

| BL site | Geometry setup immediately before BL | Status |
|---:|---|---|
| `0x0800A73E` | `r3 = 0x0A07` (10 high × 7 wide) | First-screen text labels; former-Small path |
| `0x0800F4E6` | `r3 = 0x0A07` | Former-Small path; caller context still needs semantic confirmation |
| `0x0800F95E` | `r3 = 0x0A07` | Former-Small path; caller context still needs semantic confirmation |
| `0x0800F99C` | `r3 = 0x0A07` | Former-Small path; caller context still needs semantic confirmation |

These sites were not changed in v9. The v9 builder only made the Small descriptor 5×8 and redirected it to `0x0800F648`; it explicitly left renderer calls, coordinates and dimensions untouched. Consequently, the four callers still use a 7×10 dimension argument against a 5×8 Small table. This is a real mismatch, but correcting just those four does not yet prove all small-font paths are unified.

The home clock and battery numeric draws at `0x0800AAD2` and `0x0800AAE8` also call `0x08008E48`, but use different caller setup. The former-Small fix history changed a runtime pointer at `0x0800AB24` from `0x20000054` to `0x2000004C`; that pointer's meaning has not been independently proven from the v9 binary. Do not treat that edit as a validated renderer unification.

## All scanned BL sites to the common low-level routine

```text
08009252 08009538 08009594 080099BC 080099EA 08009A14
08009BE2 08009C0A 08009DF2 08009E14 08009F9E 0800A0B6
0800A2EC 0800A73E 0800A898 0800A8E4 0800AAA6 0800AAD2
0800AAE8 0800C258 0800C2AA 0800C30E 0800C370 0800C3BE
0800C702 0800C986 0800C9A6 0800C9C6 0800CA40 0800CA7C
0800CAB8 0800CF38 0800CF5C 0800D158 0800D17E 0800D1A4
0800D402 0800D426 0800D44C 0800D6B6 0800D6DA 0800D6FE
0800D972 0800D996 0800DC74 0800DD54 0800DE38 0800E002
0800E0E6 0800E0FA 0800F0FC 0800F118 0800F23A 0800F4E6
0800F95E 0800F99C 0800FDDA
```

This is the full aligned Thumb BL scan within the v9 firmware's `0x08008000–0x0800FFFF` region for target `0x08008E48`. It is a call-site inventory, not yet a semantic classification of every caller as Small, Tiny, Medium, icon-adjacent text, or dynamically configured text.

## Font data and text inventory already verified in v9

- The v9 Small descriptor at `0x0800FD08` is rewritten to 5×8 and table `0x0800F648`.
- The Tiny descriptor at `0x0800FD10` remains unchanged and points to its original table at `0x0800F9D4`.
- The original first-screen `µSv/h`, `CPM`, and `µR/h` labels were 60×20 rasters. They were converted to text well before v9. In v9, the text strings at `0x0800A748`, `0x0800A74E`, and `0x0800A752` are live inputs to the first-screen former-Small draw call at `0x0800A73E`; they are not the old raster labels. The v9 builder also rewrites dose/settings strings around `0x0800F938–0x0800F9D0`.
- Energy/Statistics are known to use the native Tiny path; the moved decimal-point glyph was specifically intended to repair Statistics. Preserve this behavior while unifying the rest.

## Still required before editing a unified-render HEX

1. Trace all 57 callers' argument setup and reachable string sources, including calls that load descriptors through RAM rather than literal flash pointers.
2. Classify each call by actual visible screen and font role; identify every Small and Tiny caller and exclude Medium/bitmap drawing with evidence.
3. Preserve the current text implementation of the home unit labels and distinguish it from the original 60×20 raster assets; separately map dose-screen `µSv`/`CPM`/`µR` strings.
4. Choose one canonical small-font path (same renderer entry, descriptor/geometry, foreground/background and clearing behavior), then route every classified small-font caller to it. Preserve screen-specific coordinates and non-small renderers.
5. Verify that the obsolete former-Small route has no remaining small-font callers before removing it.

No firmware bytes were changed for this inventory. In particular, v17/v18 pointer/color patches are not evidence of a complete all-small-font unification.
