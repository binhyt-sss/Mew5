#pragma once
#include <stdint.h>

#define LCD_W 240
#define LCD_H 320

/* Init LCD (opens SPI, resets display, sends init sequence).
 * Returns 0 on success, -1 on error. */
int  ili9341_open(void);
void ili9341_close(void);

/* Fill entire screen with a RGB565 color. */
void ili9341_fill(uint16_t color);

/* Draw raw RGB888 buffer (w x h) scaled to fit LCD_W x LCD_H. */
void ili9341_draw_rgb888(const uint8_t *rgb, int w, int h);

/* Draw NV21 (YUV420sp) buffer scaled to fit LCD_W x LCD_H.
 * stride = width of Y plane in bytes (usually == w). */
void ili9341_draw_nv21(const uint8_t *nv21, int w, int h, int stride);

/* Convert single NV21 pixel to RGB565. */
uint16_t ili9341_yuv_to_565(uint8_t y, int8_t u, int8_t v);
