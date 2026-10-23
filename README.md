# Geiger-2 firmware bootstrap

STM32L051C8T6 firmware for the Geiger-2. The current prototype displays rolling dose rate and accumulated dose, reads detector pulses and ADC channels, and drives the LCD through SPI2/DMA. Project status and technical references are indexed in [`docs/README.md`](docs/README.md).

## Status

The LCD has two main pages: dose rate in µSv/h with CPM, and accumulated dose. A long right-click on Dose opens the current rate graph; short clicks select its range. A long right-click from Rate opens `TIME → SETTINGS → DEBUG`. In that menu, short right/left clicks navigate and long left-click returns. TIME displays uptime until a calendar time is set; the RTC clock now works after an RCC register address fix. Main pages include solar and battery percentages. The backlight times out after 10 seconds. A detector pulse briefly flashes the LED in the current dose-rate color. LCD uses SPI2 on PB13/PB15, PA8 reset, PB11 D/C, and active-low PA11 CS. Physical dose calibration and HV control remain open. SPI logging is implemented; long-term write/erase reliability on the physical NOR chip still needs verification.

The repository also contains a working browser console in [`app/`](app/README.md): live Overview, log browsing, supported settings, and Debug over Web Serial. It has no build step or external dependencies. Firmware update and time synchronization through the app are not implemented yet.

## Layout

- `include/` — portable application interfaces and configuration types.
- `src/` — measurement/configuration logic and the STM32L051 board implementation.
- `app/` — browser client (HTML/CSS/JavaScript) and run instructions.
- `include/board_config.h` — board pin constants and hardware configuration.
- `board/` — board notes and mapping status.
- `linker/` and `startup_stm32l051.S` — initial STM32L051C8 startup and memory map.

## Next steps

1. Verify detector rate against a longer reference measurement and calibrate the dose coefficient.
2. Map and verify HV control, battery/solar ADC scaling, speaker, and remaining board signals.
3. Confirm the SPI NOR geometry and verify journal read/write/erase behavior on the actual chip before relying on long-term retention.

## Build with PlatformIO

Install Visual Studio Code and the **PlatformIO IDE** extension. Open this repository folder; PlatformIO will install its STM32 platform and ARM toolchain on the first build. From the PlatformIO sidebar, select **Build**, or run:

```powershell
pio run
```

The output files are under `.pio/build/geiger2/` (`firmware.elf`, `firmware.hex`, and `firmware.bin`). The project uses a local board definition and its own startup file and linker script; it is bare metal and does not require Arduino or STM32 HAL.

The committed default upload profile uses ST-LINK over SWD. On this workstation, the ignored `platformio.local.ini` selects UART bootloader upload on COM4 instead. Put the board in UART bootloader mode manually before starting the upload.

To use the committed ST-LINK profile, connect an ST-LINK to SWDIO, SWCLK, GND, and preferably NRST, then select **Upload** or run:

```powershell
pio run --target upload
```

The UART upload requires STM32CubeProgrammer CLI. The helper searches `PATH` and the standard Program Files locations. The USB-C connector and its boot-mode wiring have not been independently verified. For ST-LINK, keep the board powered normally and do not connect a second 3.3 V supply from the probe.

## Alternative CMake build

Install CMake, Ninja, and GNU Arm Embedded Toolchain, then run from the repository root:

```powershell
cmake --preset arm-debug
cmake --build --preset arm-debug
```

The output files are `build/geiger2.hex` and `build/geiger2.bin`.

## Alternative programming with STM32CubeProgrammer

Install ST's STM32CubeProgrammer and connect an ST-LINK to the target's SWDIO, SWCLK, GND, and preferably NRST. Keep the board powered normally; do not connect a second 3.3 V supply from the probe. With the generated HEX, run in PowerShell:

```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD -w "build\geiger2.hex" -v -rst
```

The Geiger-2 USB-C connector has not been confirmed as a programming interface. STM32L05 ROM bootloaders support USART1/USART2 and SPI; the relevant ST bootloader table does not list USB DFU for this family. A board-level USB-to-UART bridge could still expose a USART boot path if its pins and boot-mode control are wired for it. See ST's [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) and [AN2606](https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf).
