/* lcd_color_test: just fill the screen with colors to verify ST7789 wiring */
#include "st7789.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
    if (st7789_open() != 0) {
        printf("ST7789 open failed\n");
        return 1;
    }

    printf("WHITE\n");  st7789_fill(0xFFFF); sleep(2);
    printf("RED\n");    st7789_fill(0xF800); sleep(2);
    printf("GREEN\n");  st7789_fill(0x07E0); sleep(2);
    printf("BLUE\n");   st7789_fill(0x001F); sleep(2);
    printf("BLACK\n");  st7789_fill(0x0000); sleep(2);

    st7789_close();
    printf("Done\n");
    return 0;
}
