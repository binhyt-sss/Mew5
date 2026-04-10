#pragma once
#include <stdint.h>

#define LCD_W 172
#define LCD_H 320

/* Init LCD (opens SPI, resets display, sends ST7789 init sequence).
 * Returns 0 on success, -1 on error. */
int  st7789_open(void);
void st7789_close(void);

/* Fill entire screen with a RGB565 color. */
void st7789_fill(uint16_t color);

/* Draw NV21 (YUV420sp) buffer scaled to fit LCD_W x LCD_H.
 * stride = width of Y plane in bytes (usually == w). */
void st7789_draw_nv21(const uint8_t *nv21, int w, int h, int stride);
