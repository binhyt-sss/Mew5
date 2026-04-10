#include "st7789.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal JPEG decoder — use stb_image (single header) */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_SIMD
#include "stb_image.h"

int main(int argc, char *argv[]) {
    const char *img_path = (argc > 1) ? argv[1] : "/tmp/hienthi.jpg";

    /* Init display */
    if (st7789_init() != 0) return 1;

    /* Fill black first */
    st7789_fill(0x0000);

    /* Load JPEG */
    int w, h, ch;
    uint8_t *rgb = stbi_load(img_path, &w, &h, &ch, 3);
    if (!rgb) {
        fprintf(stderr, "Cannot load image: %s\n", img_path);
        st7789_fill(0xF800); /* fill red on error */
        st7789_deinit();
        return 1;
    }
    printf("Image loaded: %dx%d ch=%d\n", w, h, ch);

    /* Scale/crop to fit 172x320
     * Simple approach: crop center if too big, letterbox if too small */
    int dst_w = ST7789_WIDTH;
    int dst_h = ST7789_HEIGHT;

    /* Scale factor to fit display (maintain aspect ratio) */
    float scale_x = (float)dst_w / w;
    float scale_y = (float)dst_h / h;
    float scale   = scale_x < scale_y ? scale_x : scale_y;

    int scaled_w = (int)(w * scale);
    int scaled_h = (int)(h * scale);
    if (scaled_w < 1) scaled_w = 1;
    if (scaled_h < 1) scaled_h = 1;

    /* Offset to center on display */
    int off_x = (dst_w - scaled_w) / 2;
    int off_y = (dst_h - scaled_h) / 2;

    printf("Scaled: %dx%d -> %dx%d (offset %d,%d)\n",
           w, h, scaled_w, scaled_h, off_x, off_y);

    /* Allocate display pixel buffer */
    uint16_t *fb = (uint16_t *)calloc(dst_w * dst_h, sizeof(uint16_t));
    if (!fb) { fprintf(stderr, "OOM\n"); return 1; }

    /* Nearest-neighbor scale into fb */
    for (int dy = 0; dy < scaled_h; dy++) {
        int sy = (int)(dy / scale);
        if (sy >= h) sy = h - 1;
        for (int dx = 0; dx < scaled_w; dx++) {
            int sx = (int)(dx / scale);
            if (sx >= w) sx = w - 1;
            uint8_t *p = rgb + (sy * w + sx) * 3;
            int fx = off_x + dx;
            int fy = off_y + dy;
            if (fx >= 0 && fx < dst_w && fy >= 0 && fy < dst_h) {
                fb[fy * dst_w + fx] = rgb888_to_565(p[0], p[1], p[2]);
            }
        }
    }

    stbi_image_free(rgb);

    /* Push to display */
    printf("Drawing to ST7789...\n");
    st7789_draw_image(fb, 0, 0, dst_w, dst_h);
    free(fb);

    printf("Done!\n");
    st7789_deinit();
    return 0;
}
