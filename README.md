# Geiger-2 firmware bootstrap

Initial application scaffold for the STM32L051C8T6 target. Requirements and UI are in [`docs/docs/`](docs/docs/); this repository root contains the firmware core.

## Status

This is an early test firmware. It switches the onboard RGB LED off at startup and uses the right button to step off → red → yellow → green → cyan (green + blue) → blue → purple (red + blue) → white (all channels); the left button steps backward. Pressing right from green adds blue; pressing left from green adds red. Short presses change color on release. Holding right for 800 ms raises brightness to the next level; holding left lowers it. Levels are 1, 3, 5, 10, 30, 50, 70, and 100%, starting at 30%. The sequence stops at both ends. Confirmed RGB pins are PA0=red, PA1=green, PA6=blue, active-low; buttons are PB9=left and PA4=right, active-low. In the UI, left is Exit/Off and right is Enter. The firmware does not enable the detector HV generator or write to external flash.

## Layout

- `include/` — portable application interfaces and configuration types.
- `src/` — portable measurement/configuration logic, with no STM32 register access.
- `src/board_stm32l051.c` and `include/board_config.h` — first-try STM32 board adaptation with candidate GPIOs.
- `board/` — board notes and mapping status.
- `linker/` and `startup_stm32l051.S` — initial STM32L051C8 startup and memory map.

## Next steps

1. Build and flash the RGB/button test image with HV left disabled.
2. Check the color order and which physical key maps to each direction; correct `include/board_config.h` if needed.
3. Measure LED/button polarity and replace the candidate mapping with verified board facts.
4. Add LCD support and then connect and validate detector pulse capture.
5. Confirm the exact SPI NOR part and geometry before defining any erase or journal operations.

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
