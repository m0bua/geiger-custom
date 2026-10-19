#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define BOARD_BUTTON_LEFT  (1u << 0)
#define BOARD_BUTTON_RIGHT (1u << 1)
#define BOARD_BUTTON_LEFT_LONG  (1u << 2)
#define BOARD_BUTTON_RIGHT_LONG (1u << 3)
#define BOARD_BUTTON_LEFT_DOWN  (1u << 4)
#define BOARD_BUTTON_RIGHT_DOWN (1u << 5)

extern volatile uint32_t board_millis;

void board_init(void);
uint8_t board_buttons_sample_10ms(void);
uint32_t board_detector_count(void);
void board_adc_init(void);
uint16_t board_adc_read(uint8_t channel);
void board_adc_read_all(uint16_t *channel2, uint16_t *channel8,
                        uint16_t *channel9);
void board_rgb_set(uint8_t logical_rgb_mask);
void board_rgb_set_brightness(uint8_t percent);
void board_display_init(void);
void board_display_service(void);
void board_display_next_page(void);
void board_display_set_rate(uint32_t rate_thousandths, uint32_t cpm);
void board_display_set_dose(uint32_t dose_thousandths,
                            uint32_t dose_fraction);
void board_display_set_adc(uint16_t channel2, uint16_t channel8,
                           uint16_t channel9);
bool board_display_busy(void);
void board_lcd_backlight_set(bool enabled);
void board_lcd_backlight_set_brightness(uint8_t percent);

#endif
