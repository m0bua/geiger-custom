# Board adaptation

The RGB and button mapping is fixed from hardware bring-up. RGB is active-low:
PA0=red, PA1=green, PA6=blue. Buttons are active-low on PB9 (left) and PA4
(right). Right steps forward and adds blue from green; left steps backward and
adds red from green. Purple is followed by white (all channels); the sequence
is bounded at off and white.

Short presses change color on release. After three seconds without button
activity, the RGB LED and LCD backlight turn off. During LCD initialization or
frame rendering, the backlight stays on and the idle countdown resumes after
the update. The color selection is retained; the first short press wakes it
without advancing. Holding right for 800 ms raises both LCD backlight and RGB
brightness by 1% repeatedly; holding left lowers them. Repeats start at about
10 per second and accelerate to 50 per second over four seconds, bounded by
0–100%. Backlight starts at 100%; RGB starts at the application default of
30%. Backlight PWM uses inverted LPTIM1_OUT on PB2 at 32 kHz. A timer
configuration timeout falls back to GPIO high at 100% so the screen remains
usable. GPIO low is off. RGB PWM runs at 32 kHz:
TIM2 drives red/green on PA0/PA1, and TIM22 drives blue on PA6.

For the UI, left means Exit/Off and right means Enter. See
[`include/board_config.h`](../include/board_config.h).

Stock LCD analysis identifies an ST7735-compatible controller, SPI2 signals
on PB13/PB15, PA8 reset, PB11 D/C, and active-low PA11 chip select. Stock
drawing code uses a 160×80 logical area with a 24-pixel Y offset. PA15 is
not the LCD chip select. Backlight PB2 starts low/off and is enabled by a
button press. The LCD code uses SPI2 on PB13/PB15 and checks CS/RESET/D/C
levels at the MCU. The user has confirmed the stock-sequence image displays
pixels. The 8 MHz SPI row-transfer and 16 MHz SPI area-stream builds are
confirmed working. The current DMA fill is confirmed working and very fast.
Two earlier DMA issues were fixed: SPI2_TX selector and DMA clock register
address. Earlier variants included hangs; record new
observed outcomes in
[`docs/docs/LCD_BRINGUP.md`](../docs/docs/LCD_BRINGUP.md). Three-second idle
timeout and button wake have been tested. The continuous accelerated brightness
change is a new experiment and still needs hardware verification.
The exact panel model, RGB/BGR interpretation, and orientation still need
visual confirmation. The firmware does not enable the
detector HV generator or write to external flash. See
[`docs/docs/OPEN_ITEMS.md`](../docs/docs/OPEN_ITEMS.md) for the remaining
hardware map.
