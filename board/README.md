# Board adaptation

The RGB and button mapping is fixed from hardware bring-up. RGB is active-low:
PA0=red, PA1=green, PA6=blue. Buttons are active-low on PB9 (left) and PA4
(right). Right steps forward and adds blue from green; left steps backward and
adds red from green. Purple is followed by white (all channels); the sequence
is bounded at off and white.

Short presses change color on release. After one second without button
activity, the RGB LED and LCD backlight turn off. During LCD initialization or
frame rendering, the backlight stays on and the idle countdown resumes after
the update. The color selection is
retained; the first short press wakes it without advancing. Holding right for
800 ms raises brightness to the next level; holding left lowers it. Levels are
1, 3, 5, 10, 30, 50, 70, and 100%, starting at the application default of 30%.
RGB PWM runs at 16 kHz:
TIM2 drives red/green on PA0/PA1, and TIM22 drives blue on PA6.

For the UI, left means Exit/Off and right means Enter. See
[`include/board_config.h`](../include/board_config.h).

Stock LCD analysis identifies an ST7735-compatible controller, SPI2 signals
on PB13/PB15, PA8 reset, PB11 D/C, and active-low PA11 chip select. Stock
drawing code uses a 160×80 logical area with a 24-pixel Y offset. PA15 is
not the LCD chip select. Backlight PB2 starts low/off and is enabled by a
button press. The current diagnostic firmware uses SPI2 on PB13/PB15 and
checks for signal transitions plus CS/RESET/D/C output levels at the MCU. The
user has confirmed green diagnostic LED and a still blank LCD. This check does
not prove continuity to the panel or command acceptance. Earlier variants
included hangs; record new observed outcomes in
[`docs/docs/LCD_BRINGUP.md`](../docs/docs/LCD_BRINGUP.md). One-second idle
timeout and button wake have been tested. Brightness steps apply only to the
RGB LED.
The exact panel model, RGB/BGR interpretation, and orientation still need
visual confirmation. The firmware does not enable the
detector HV generator or write to external flash. See
[`docs/docs/OPEN_ITEMS.md`](../docs/docs/OPEN_ITEMS.md) for the remaining
hardware map.
