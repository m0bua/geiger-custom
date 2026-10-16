# Geiger-2 firmware bootstrap

Initial application scaffold for the STM32L051C8T6 target. Requirements and UI are in [`docs/docs/`](docs/docs/); this repository root contains the firmware core.

## Status

This is an application skeleton, not a flashable image. Clock/startup, linker script, board GPIO, LCD, buttons, detector input, analog measurements, external SPI NOR, and HV control are intentionally not configured until the board signals and safe states are verified. Nothing in this scaffold enables the HV generator or writes to external flash.

## Layout

- `include/` — portable application interfaces and configuration types.
- `src/` — portable measurement/configuration logic, with no STM32 register access.
- `board/` — reserved for the verified STM32L051 board adaptation.

## First implementation sequence

1. Record verified MCU pin, polarity, reset state, and peripheral mapping in `board/`.
2. Add STM32CubeL0/CMSIS startup and a linker script matching STM32L051C8T6 (64 KiB Flash, 8 KiB SRAM); do not copy the legacy firmware image map.
3. Bring up clock, uptime tick, display, and buttons with detector/HV outputs held at verified safe levels.
4. Validate detector pulse capture with a known source, then connect it to the portable measurement core.
5. Confirm the exact SPI NOR part and geometry before defining any erase or journal operations.

Build integration is deliberately deferred until the repository selects a toolchain and STM32CubeL0 dependency/version. The portable sources are plain C11 and can be added to the eventual target without importing a browser transport or unverified pin map.
