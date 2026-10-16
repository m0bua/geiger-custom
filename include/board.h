#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define BOARD_BUTTON_LEFT  (1u << 0)
#define BOARD_BUTTON_RIGHT (1u << 1)
#define BOARD_BUTTON_LEFT_LONG  (1u << 2)
#define BOARD_BUTTON_RIGHT_LONG (1u << 3)

extern volatile uint32_t board_millis;

void board_init(void);
uint8_t board_buttons_sample_10ms(void);
void board_rgb_set(uint8_t logical_rgb_mask);
void board_rgb_set_brightness(uint8_t percent);

#endif
