# Board adaptation

The confirmed button mapping is active-low PB9=left and PA4=right. The RGB LED
is active-low: PA0=red, PA1=green, PA6=blue. The current measurement image
keeps RGB off except for a brief green blink on each detector pulse.

The LCD prototype cycles between dose-rate/CPM and accumulated-dose pages.
Both pages share ADC8, solar ADC9, and battery ADC2 diagnostics at the top.
Buttons switch pages and wake the backlight. Backlight PWM uses inverted
LPTIM1_OUT on PB2 at 32 kHz, starts off, and times out after 10 seconds.
RGB PWM runs at 32 kHz: TIM2 drives red/green on PA0/PA1, and TIM22 drives blue
on PA6.

For the UI, left means Exit/Off and right means Enter. See
[`include/board_config.h`](../include/board_config.h).

Stock LCD analysis identifies an ST7735-compatible command set, SPI2 signals
on PB13/PB15, PA8 reset, PB11 D/C, and active-low PA11 chip select. Custom
rendering uses a 160×80 logical area with Y offset 28; the stock code uses 24.
The stock init sequence and current display status are in
[`docs/docs/LCD_AND_DISPLAY.md`](../docs/docs/LCD_AND_DISPLAY.md). The exact
panel model, RGB/BGR interpretation, and orientation still need visual
confirmation. Dose conversion has not been validated against a reference
source. HV control and external flash writes are not implemented. See
[`docs/docs/OPEN_ITEMS.md`](../docs/docs/OPEN_ITEMS.md) for the remaining
hardware map.
