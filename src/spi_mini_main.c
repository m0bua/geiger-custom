#include "board.h"
#include "spi_nor.h"

void protocol_v1_service(void);

int main(void)
{
    board_init();
    board_rgb_set_color(255u, 0u, 255u);
    board_rgb_set_brightness(10u);
    (void)spi_nor_init();
    for (;;) protocol_v1_service();
}
