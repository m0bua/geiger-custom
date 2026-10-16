#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Geiger-2 mapping fixed from hardware bring-up. RGB outputs are active-low.
 * Stock HEX also reads the button inputs on PA4 and PB9. */
#define BOARD_RGB_R_PIN             0u  /* PA0 */
#define BOARD_RGB_G_PIN             1u  /* PA1 */
#define BOARD_RGB_B_PIN             6u  /* PA6 */
#define BOARD_BUTTON_LEFT_PIN       9u  /* PB9; reverse / UI Exit-Off */
#define BOARD_BUTTON_RIGHT_PIN      4u  /* PA4; forward / enter */

/* Stock HEX drives PB2 high in active paths and low before LCD SLPIN.
 * Treat it as a candidate active-high LCD/backlight enable for bring-up. */
#define BOARD_LCD_BACKLIGHT_PIN     2u  /* PB2; candidate on/off control */

/* Hardware test showed the RGB LED is active-low: driving all channels low
 * produces white, so the inactive state must be high. */
#define BOARD_RGB_ACTIVE_HIGH       0u
#define BOARD_BUTTON_ACTIVE_LOW     1u

#endif
