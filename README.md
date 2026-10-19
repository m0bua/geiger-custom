# Geiger-2 firmware bootstrap

STM32L051C8T6 firmware for the Geiger-2. The current prototype verifies the RGB LED, buttons, LCD initialization and fast color fills, and backlight PWM. Project notes and component test history are indexed in [`docs/README.md`](docs/README.md).

## Status

The RGB LED starts off and steps through off → red → yellow → green → cyan → blue → purple → white; right advances and left reverses. The LCD initializes at startup with its backlight off. A button turns on the backlight; after three seconds without button activity, the RGB LED and backlight turn off. The color selection is retained; the first short press wakes the current color without advancing. Holding right for 800 ms raises RGB and backlight brightness by 1% repeatedly; holding left lowers both. Repeats start at about 10 per second and accelerate to 50 per second over four seconds. RGB brightness starts at 30%; backlight brightness starts at 100%. Confirmed RGB pins are PA0=red, PA1=green, PA6=blue, active-low; buttons are PB9=left and PA4=right, active-low. LCD uses SPI2 on PB13/PB15, PA8 reset, PB11 D/C, and active-low PA11 CS. See [display bring-up log](docs/docs/LCD_BRINGUP.md). Detector pulse capture, HV control, and external flash writes are not implemented.

## Layout

- `include/` — portable application interfaces and configuration types.
- `src/` — portable measurement/configuration logic, with no STM32 register access.
- `src/board_stm32l051.c` and `include/board_config.h` — first-try STM32 board adaptation with candidate GPIOs.
- `board/` — board notes and mapping status.
- `linker/` and `startup_stm32l051.S` — initial STM32L051C8 startup and memory map.

## Next steps

1. Establish the detector pulse input and safe interrupt/timer configuration.
2. Map and verify the HV, battery, solar input, speaker, and remaining board signals.
3. Confirm the exact SPI NOR part and geometry before defining any erase or journal operations.

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
