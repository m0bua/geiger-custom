# Board adaptation

The RGB and button mapping is fixed from hardware bring-up. RGB is active-low:
PA0=red, PA1=green, PA6=blue. Buttons are active-low on PB9 (left) and PA4
(right). Right steps forward and adds blue from green; left steps backward and
adds red from green. Purple is followed by white (all channels); the sequence
is bounded at off and white.

Short presses change color on release. Holding right for 800 ms raises
brightness to the next level; holding left lowers it. Levels are 1, 3, 5, 10,
30, 50, 70, and 100%, starting at the application default of 30%. RGB PWM runs at 16 kHz:
TIM2 drives red/green on PA0/PA1, and TIM22 drives blue on PA6.

For the UI, left means Exit/Off and right means Enter. See
[`include/board_config.h`](../include/board_config.h).

The board layer only touches RGB and button pins. It does not enable the
detector HV generator, initialize the LCD, or write to external flash. See
[`docs/docs/OPEN_ITEMS.md`](../docs/docs/OPEN_ITEMS.md) for the remaining
hardware map.
