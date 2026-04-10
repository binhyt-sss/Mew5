#pragma once
#include <stdint.h>

#define LCD_W 320
#define LCD_H 240

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

/* Draw NV21 frame with bounding boxes + keypoints rendered into an intermediate framebuffer.
 * boxes: flat [x0,y0,x1,y1] normalized [0..1], nboxes entries.
 * kpts: flat [x,y] normalized [0..1] per hand, nkpts_per_hand keypoints per hand.
 * box_color: e.g. 0x07E0 = green, kpt_color: e.g. 0xF800 = red */
void ili9341_draw_nv21_with_boxes(const uint8_t *nv21, int w, int h, int stride,
                                  const float *boxes, int nboxes, uint16_t box_color);

void ili9341_draw_nv21_with_boxes_kpts(const uint8_t *nv21, int w, int h, int stride,
                                       const float *boxes, int nboxes, uint16_t box_color,
                                       const float *kpts, int nkpts_per_hand,
                                       uint16_t kpt_color);

/* Draw blank layout with bounding boxes only (no camera frame).
 * boxes: flat [x0,y0,x1,y1] normalized [0..1], nboxes entries. */
void ili9341_draw_boxes_only(const float *boxes, int nboxes,
                             uint16_t bg_color565, uint16_t box_color565);

/* Draw NV21 frame with bounding boxes overlaid.
 * boxes: flat array of [x0,y0,x1,y1] normalized [0..1], nboxes entries.
 * box_color565: e.g. 0x07E0 = green */
void ili9341_draw_nv21_boxes(const uint8_t *nv21, int w, int h, int stride,
                             const float *boxes, int nboxes, uint16_t box_color565);
