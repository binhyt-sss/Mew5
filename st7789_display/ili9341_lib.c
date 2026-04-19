#include "ili9341.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_DEV   "/dev/spidev0.0"
#define GPIO_DC   503   /* GP23 = Pin 21 */
#define GPIO_RST  504   /* GP24 = Pin 22 */

/* ---------- GPIO (persistent fd for speed) ---------- */
static int gpio_dc_fd  = -1;
static int gpio_rst_fd = -1;

static void gpio_export(int g) {
    char b[32];
    int f = open("/sys/class/gpio/export", O_WRONLY);
    if (f < 0) return;
    snprintf(b, 32, "%d", g);
    write(f, b, strlen(b));
    close(f);
}
static void gpio_dir(int g, int out) {
    char p[64];
    snprintf(p, 64, "/sys/class/gpio/gpio%d/direction", g);
    int f = open(p, O_WRONLY);
    if (f < 0) { gpio_export(g); f = open(p, O_WRONLY); }
    if (f < 0) return;
    write(f, out ? "out" : "in", out ? 3 : 2);
    close(f);
}
static int gpio_open_value(int g) {
    char p[64];
    snprintf(p, 64, "/sys/class/gpio/gpio%d/value", g);
    return open(p, O_WRONLY);
}
static inline void gpio_set_fd(int fd, int v) {
    lseek(fd, 0, SEEK_SET);
    write(fd, v ? "1" : "0", 1);
}
static void gpio_set(int g, int v) {
    int fd = (g == GPIO_DC) ? gpio_dc_fd : gpio_rst_fd;
    if (fd >= 0) { gpio_set_fd(fd, v); return; }
    /* fallback: open/close */
    char p[64];
    snprintf(p, 64, "/sys/class/gpio/gpio%d/value", g);
    int f = open(p, O_WRONLY);
    if (f < 0) return;
    write(f, v ? "1" : "0", 1);
    close(f);
}

/* ---------- SPI ---------- */
static int spi_fd = -1;

static void spi_send(const uint8_t *buf, int len) {
    struct spi_ioc_transfer t = {
        .tx_buf = (unsigned long)buf,
        .rx_buf = 0,
        .len = len,
        .speed_hz = 40000000,
        .bits_per_word = 8,
    };
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &t);
}

static void lcd_cmd(uint8_t c) {
    gpio_set(GPIO_DC, 0);
    spi_send(&c, 1);
}
static void lcd_dat1(uint8_t d) {
    gpio_set(GPIO_DC, 1);
    spi_send(&d, 1);
}
static void lcd_dat(const uint8_t *d, int len) {
    gpio_set(GPIO_DC, 1);
    spi_send(d, len);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_cmd(0x2A);
    lcd_dat1(x0 >> 8); lcd_dat1(x0 & 0xFF);
    lcd_dat1(x1 >> 8); lcd_dat1(x1 & 0xFF);
    lcd_cmd(0x2B);
    lcd_dat1(y0 >> 8); lcd_dat1(y0 & 0xFF);
    lcd_dat1(y1 >> 8); lcd_dat1(y1 & 0xFF);
    lcd_cmd(0x2C);
}

/* ---------- ILI9341 init ---------- */
static void ili9341_hw_init(void) {
    gpio_set(GPIO_RST, 1); usleep(10000);
    gpio_set(GPIO_RST, 0); usleep(20000);
    gpio_set(GPIO_RST, 1); usleep(150000);

    lcd_cmd(0x01); usleep(100000);

    lcd_cmd(0xCB); lcd_dat1(0x39); lcd_dat1(0x2C); lcd_dat1(0x00); lcd_dat1(0x34); lcd_dat1(0x02);
    lcd_cmd(0xCF); lcd_dat1(0x00); lcd_dat1(0xC1); lcd_dat1(0x30);
    lcd_cmd(0xE8); lcd_dat1(0x85); lcd_dat1(0x00); lcd_dat1(0x78);
    lcd_cmd(0xEA); lcd_dat1(0x00); lcd_dat1(0x00);
    lcd_cmd(0xED); lcd_dat1(0x64); lcd_dat1(0x03); lcd_dat1(0x12); lcd_dat1(0x81);
    lcd_cmd(0xF7); lcd_dat1(0x20);
    lcd_cmd(0xC0); lcd_dat1(0x23);
    lcd_cmd(0xC1); lcd_dat1(0x10);
    lcd_cmd(0xC5); lcd_dat1(0x3E); lcd_dat1(0x28);
    lcd_cmd(0xC7); lcd_dat1(0x86);
    lcd_cmd(0x36); lcd_dat1(0xA8); /* landscape 320x240, Y-flipped */
    lcd_cmd(0x3A); lcd_dat1(0x55);
    lcd_cmd(0xB1); lcd_dat1(0x00); lcd_dat1(0x18);
    lcd_cmd(0xB6); lcd_dat1(0x08); lcd_dat1(0x82); lcd_dat1(0x27);
    lcd_cmd(0xF2); lcd_dat1(0x00);
    lcd_cmd(0x26); lcd_dat1(0x01);
    lcd_cmd(0xE0);
    lcd_dat1(0x0F); lcd_dat1(0x31); lcd_dat1(0x2B); lcd_dat1(0x0C);
    lcd_dat1(0x0E); lcd_dat1(0x08); lcd_dat1(0x4E); lcd_dat1(0xF1);
    lcd_dat1(0x37); lcd_dat1(0x07); lcd_dat1(0x10); lcd_dat1(0x03);
    lcd_dat1(0x0E); lcd_dat1(0x09); lcd_dat1(0x00);
    lcd_cmd(0xE1);
    lcd_dat1(0x00); lcd_dat1(0x0E); lcd_dat1(0x14); lcd_dat1(0x03);
    lcd_dat1(0x11); lcd_dat1(0x07); lcd_dat1(0x31); lcd_dat1(0xC1);
    lcd_dat1(0x48); lcd_dat1(0x08); lcd_dat1(0x0F); lcd_dat1(0x0C);
    lcd_dat1(0x31); lcd_dat1(0x36); lcd_dat1(0x0F);
    lcd_cmd(0x11); usleep(120000);
    lcd_cmd(0x29);
}

/* ---------- Public API ---------- */

int ili9341_open(void) {
    gpio_dir(GPIO_DC,  1);
    gpio_dir(GPIO_RST, 1);

    spi_fd = open(SPI_DEV, O_RDWR);
    if (spi_fd < 0) { perror("ili9341: open spi"); return -1; }

    uint8_t mode = SPI_MODE_0, bits = 8;
    uint32_t spd = 40000000;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spd);

    gpio_dc_fd  = gpio_open_value(GPIO_DC);
    gpio_rst_fd = gpio_open_value(GPIO_RST);

    ili9341_hw_init();
    printf("ILI9341 init OK (%dx%d)\n", LCD_W, LCD_H);
    return 0;
}

void ili9341_close(void) {
    if (spi_fd >= 0)    { close(spi_fd);    spi_fd = -1; }
    if (gpio_dc_fd >= 0)  { close(gpio_dc_fd);  gpio_dc_fd  = -1; }
    if (gpio_rst_fd >= 0) { close(gpio_rst_fd); gpio_rst_fd = -1; }
}

void ili9341_fill(uint16_t color) {
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set(GPIO_DC, 1);
    uint8_t buf[256];
    for (int i = 0; i < 256; i += 2) {
        buf[i]   = color >> 8;
        buf[i+1] = color & 0xFF;
    }
    int total = LCD_W * LCD_H * 2, sent = 0;
    while (sent < total) {
        int n = (total - sent) > 256 ? 256 : (total - sent);
        spi_send(buf, n);
        sent += n;
    }
}

void ili9341_draw_rgb888(const uint8_t *rgb, int w, int h) {
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set(GPIO_DC, 1);
    uint8_t buf[512];
    int bp = 0;
    for (int dy = 0; dy < LCD_H; dy++) {
        int sy = dy * h / LCD_H;
        for (int dx = 0; dx < LCD_W; dx++) {
            int sx = dx * w / LCD_W;
            const uint8_t *p = rgb + (sy * w + sx) * 3;
            uint16_t c = ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3);
            buf[bp++] = c >> 8;
            buf[bp++] = c & 0xFF;
            if (bp == 512) { spi_send(buf, 512); bp = 0; }
        }
    }
    if (bp > 0) spi_send(buf, bp);
}

uint16_t ili9341_yuv_to_565(uint8_t y, int8_t u, int8_t v) {
    /* BT.601 limited range (Y: 16-235, UV: 16-240) */
    int yy = (int)y - 16;
    int r = (yy * 298 + v * 409 + 128) >> 8;
    int g = (yy * 298 - u * 100 - v * 208 + 128) >> 8;
    int b = (yy * 298 + u * 516 + 128) >> 8;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    /* MADCTL BGR=1: display swaps R↔B internally, so send normal RGB565 */
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/* Static framebuffer: LCD_W * LCD_H * 2 bytes = 320*240*2 = 153600 bytes */
static uint16_t s_fb[LCD_W * LCD_H];

void ili9341_draw_nv21(const uint8_t *nv21, int w, int h, int stride) {
    const uint8_t *y_plane  = nv21;
    const uint8_t *vu_plane = nv21 + stride * h;

    /* Set window then DC=1 once — keep DC high for entire frame data */
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set(GPIO_DC, 1);

    /* Render all pixels into a single large buffer, send in 4096-byte chunks */
    static uint8_t buf[4096];
    int bp = 0;

    for (int dy = 0; dy < LCD_H; dy++) {
        int sy = dy * h / LCD_H;
        for (int dx = 0; dx < LCD_W; dx++) {
            int sx = dx * w / LCD_W;
            uint8_t yv = y_plane[sy * stride + sx];
            int uv_off = (sy / 2) * stride + (sx & ~1);
            int8_t vv = (int8_t)(vu_plane[uv_off]     - 128);
            int8_t uv = (int8_t)(vu_plane[uv_off + 1] - 128);
            uint16_t c = ili9341_yuv_to_565(yv, uv, vv);
            buf[bp++] = c >> 8;
            buf[bp++] = c & 0xFF;
            if (bp == 4096) { spi_send(buf, 4096); bp = 0; }
        }
    }
    if (bp > 0) spi_send(buf, bp);
}

void ili9341_draw_nv21_with_boxes(const uint8_t *nv21, int w, int h, int stride,
                                  const float *boxes, int nboxes, uint16_t box_color) {
    const uint8_t *y_plane  = nv21;
    const uint8_t *vu_plane = nv21 + stride * h;

    /* Render NV21 + boxes into framebuffer, store as big-endian (high byte first) */
    for (int dy = 0; dy < LCD_H; dy++) {
        int sy = dy * h / LCD_H;
        for (int dx = 0; dx < LCD_W; dx++) {
            int sx = dx * w / LCD_W;
            uint8_t yv = y_plane[sy * stride + sx];
            int uv_off = (sy / 2) * stride + (sx & ~1);
            int8_t vv = (int8_t)(vu_plane[uv_off]     - 128);
            int8_t uv = (int8_t)(vu_plane[uv_off + 1] - 128);
            uint16_t c = ili9341_yuv_to_565(yv, uv, vv);
            /* Store big-endian: high byte in [0], low byte in [1] */
            uint8_t *p = (uint8_t *)&s_fb[dy * LCD_W + dx];
            p[0] = c >> 8;
            p[1] = c & 0xFF;
        }
    }

    /* Draw boxes onto framebuffer */
    for (int b = 0; b < nboxes; b++) {
        int bx0 = (int)(boxes[b*4+0] * LCD_W); if (bx0 < 0) bx0 = 0;
        int by0 = (int)(boxes[b*4+1] * LCD_H); if (by0 < 0) by0 = 0;
        int bx1 = (int)(boxes[b*4+2] * LCD_W); if (bx1 >= LCD_W) bx1 = LCD_W - 1;
        int by1 = (int)(boxes[b*4+3] * LCD_H); if (by1 >= LCD_H) by1 = LCD_H - 1;
        uint8_t bhi = box_color >> 8, blo = box_color & 0xFF;
        /* Top and bottom edges */
        for (int x = bx0; x <= bx1; x++) {
            uint8_t *t = (uint8_t *)&s_fb[by0 * LCD_W + x];
            t[0] = bhi; t[1] = blo;
            uint8_t *bt = (uint8_t *)&s_fb[by1 * LCD_W + x];
            bt[0] = bhi; bt[1] = blo;
        }
        /* Left and right edges */
        for (int y = by0; y <= by1; y++) {
            uint8_t *l = (uint8_t *)&s_fb[y * LCD_W + bx0];
            l[0] = bhi; l[1] = blo;
            uint8_t *r = (uint8_t *)&s_fb[y * LCD_W + bx1];
            r[0] = bhi; r[1] = blo;
        }
    }

    /* Send framebuffer to LCD in 4096-byte chunks */
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set(GPIO_DC, 1);
    {
        const uint8_t *p = (const uint8_t *)s_fb;
        int rem = LCD_W * LCD_H * 2;
        while (rem > 0) {
            int n = rem > 4096 ? 4096 : rem;
            spi_send(p, n);
            p += n; rem -= n;
        }
    }
}

/* ---------- 8x8 bitmap font (ASCII 32..126) ----------
 * Each character is 8 bytes, one bit per column, MSB = top row.
 * Source: public-domain font8x8_basic (subset). */
static const uint8_t s_font8x8[95][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32 ' ' */
  {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 33 '!' */
  {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* 34 '"' */
  {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* 35 '#' */
  {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* 36 '$' */
  {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* 37 '%' */
  {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* 38 '&' */
  {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* 39 ''' */
  {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* 40 '(' */
  {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* 41 ')' */
  {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 42 '*' */
  {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* 43 '+' */
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* 44 ',' */
  {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* 45 '-' */
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* 46 '.' */
  {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* 47 '/' */
  {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 48 '0' */
  {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 49 '1' */
  {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 50 '2' */
  {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 51 '3' */
  {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 52 '4' */
  {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 53 '5' */
  {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 54 '6' */
  {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 55 '7' */
  {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 56 '8' */
  {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 57 '9' */
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* 58 ':' */
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* 59 ';' */
  {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* 60 '<' */
  {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* 61 '=' */
  {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* 62 '>' */
  {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* 63 '?' */
  {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* 64 '@' */
  {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* 65 'A' */
  {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* 66 'B' */
  {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* 67 'C' */
  {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* 68 'D' */
  {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* 69 'E' */
  {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* 70 'F' */
  {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* 71 'G' */
  {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* 72 'H' */
  {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 73 'I' */
  {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* 74 'J' */
  {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* 75 'K' */
  {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* 76 'L' */
  {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* 77 'M' */
  {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* 78 'N' */
  {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* 79 'O' */
  {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* 80 'P' */
  {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* 81 'Q' */
  {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* 82 'R' */
  {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* 83 'S' */
  {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 84 'T' */
  {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* 85 'U' */
  {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 86 'V' */
  {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* 87 'W' */
  {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* 88 'X' */
  {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* 89 'Y' */
  {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* 90 'Z' */
  {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* 91 '[' */
  {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* 92 '\' */
  {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* 93 ']' */
  {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* 94 '^' */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 95 '_' */
  {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* 96 '`' */
  {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* 97 'a' */
  {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* 98 'b' */
  {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* 99 'c' */
  {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, /* 100 'd' */
  {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, /* 101 'e' */
  {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, /* 102 'f' */
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* 103 'g' */
  {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* 104 'h' */
  {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* 105 'i' */
  {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* 106 'j' */
  {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* 107 'k' */
  {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 108 'l' */
  {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* 109 'm' */
  {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* 110 'n' */
  {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* 111 'o' */
  {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* 112 'p' */
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* 113 'q' */
  {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* 114 'r' */
  {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* 115 's' */
  {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* 116 't' */
  {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* 117 'u' */
  {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 118 'v' */
  {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* 119 'w' */
  {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* 120 'x' */
  {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* 121 'y' */
  {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* 122 'z' */
  {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* 123 '{' */
  {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 124 '|' */
  {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* 125 '}' */
  {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* 126 '~' */
};

/* Draw text string into s_fb at pixel (x,y). Scale=1 → 8×8px per char. */
static void fb_text(int x, int y, const char *str, uint8_t hi, uint8_t lo) {
    int cx = x;
    for (const char *p = str; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 32 || ch > 126) { cx += 8; continue; }
        const uint8_t *glyph = s_font8x8[ch - 32];
        for (int row = 0; row < 8; row++) {
            int py = y + (7 - row);  /* flip Y */
            if (py < 0 || py >= LCD_H) continue;
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col))) continue;
                int px = cx + (7 - col);  /* flip X within glyph */
                if (px < 0 || px >= LCD_W) continue;
                uint8_t *fp = (uint8_t *)&s_fb[py * LCD_W + px];
                fp[0] = hi; fp[1] = lo;
            }
        }
        cx += 8;
    }
}

/* Helper: draw a filled 3x3 dot at (cx,cy) into s_fb (big-endian) */
static void fb_dot(int cx, int cy, uint8_t hi, uint8_t lo) {
    for (int dy = -1; dy <= 1; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= LCD_H) continue;
        for (int dx = -1; dx <= 1; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= LCD_W) continue;
            uint8_t *p = (uint8_t *)&s_fb[y * LCD_W + x];
            p[0] = hi; p[1] = lo;
        }
    }
}

void ili9341_draw_nv21_with_boxes_kpts(const uint8_t *nv21, int w, int h, int stride,
                                       const float *boxes, int nboxes, uint16_t box_color,
                                       const float *kpts, int nkpts_per_hand,
                                       uint16_t kpt_color,
                                       const char **labels, uint16_t label_color) {
    const uint8_t *y_plane  = nv21;
    const uint8_t *vu_plane = nv21 + stride * h;

    /* Render NV21 into framebuffer */
    for (int dy = 0; dy < LCD_H; dy++) {
        int sy = dy * h / LCD_H;
        for (int dx = 0; dx < LCD_W; dx++) {
            int sx = dx * w / LCD_W;
            uint8_t yv = y_plane[sy * stride + sx];
            int uv_off = (sy / 2) * stride + (sx & ~1);
            int8_t vv = (int8_t)(vu_plane[uv_off]     - 128);
            int8_t uv = (int8_t)(vu_plane[uv_off + 1] - 128);
            uint16_t c = ili9341_yuv_to_565(yv, uv, vv);
            uint8_t *p = (uint8_t *)&s_fb[dy * LCD_W + dx];
            p[0] = c >> 8;
            p[1] = c & 0xFF;
        }
    }

    /* Draw bounding boxes */
    for (int b = 0; b < nboxes; b++) {
        int bx0 = (int)(boxes[b*4+0] * LCD_W); if (bx0 < 0) bx0 = 0;
        int by0 = (int)(boxes[b*4+1] * LCD_H); if (by0 < 0) by0 = 0;
        int bx1 = (int)(boxes[b*4+2] * LCD_W); if (bx1 >= LCD_W) bx1 = LCD_W - 1;
        int by1 = (int)(boxes[b*4+3] * LCD_H); if (by1 >= LCD_H) by1 = LCD_H - 1;
        uint8_t bhi = box_color >> 8, blo = box_color & 0xFF;
        for (int x = bx0; x <= bx1; x++) {
            uint8_t *t = (uint8_t *)&s_fb[by0 * LCD_W + x]; t[0] = bhi; t[1] = blo;
            uint8_t *bt = (uint8_t *)&s_fb[by1 * LCD_W + x]; bt[0] = bhi; bt[1] = blo;
        }
        for (int y = by0; y <= by1; y++) {
            uint8_t *l = (uint8_t *)&s_fb[y * LCD_W + bx0]; l[0] = bhi; l[1] = blo;
            uint8_t *r = (uint8_t *)&s_fb[y * LCD_W + bx1]; r[0] = bhi; r[1] = blo;
        }
    }

    /* Draw keypoints: nboxes hands, each with nkpts_per_hand keypoints */
    uint8_t khi = kpt_color >> 8, klo = kpt_color & 0xFF;
    for (int b = 0; b < nboxes; b++) {
        const float *hand_kpts = kpts + b * nkpts_per_hand * 2;
        for (int k = 0; k < nkpts_per_hand; k++) {
            int kx = (int)(hand_kpts[k*2+0] * LCD_W);
            int ky = (int)(hand_kpts[k*2+1] * LCD_H);
            fb_dot(kx, ky, khi, klo);
        }
    }

    /* Draw labels above each bounding box */
    if (labels != NULL) {
        uint8_t lhi = label_color >> 8, llo = label_color & 0xFF;
        for (int b = 0; b < nboxes; b++) {
            if (labels[b] == NULL) continue;
            int bx0 = (int)(boxes[b*4+0] * LCD_W); if (bx0 < 0) bx0 = 0;
            int by0 = (int)(boxes[b*4+1] * LCD_H); if (by0 < 0) by0 = 0;
            int ty = by0 - 10;  /* 10px above box top */
            if (ty < 0) ty = by0 + 2;
            fb_text(bx0, ty, labels[b], lhi, llo);
        }
    }

    /* Send framebuffer to LCD */
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set(GPIO_DC, 1);
    {
        const uint8_t *p = (const uint8_t *)s_fb;
        int rem = LCD_W * LCD_H * 2;
        while (rem > 0) {
            int n = rem > 4096 ? 4096 : rem;
            spi_send(p, n);
            p += n; rem -= n;
        }
    }
}

/* Draw a blank layout with bounding boxes (no camera frame needed).
 * bg_color565: background color, box_color565: box edge color.
 * boxes: flat [x0,y0,x1,y1] normalized [0..1], nboxes entries. */
void ili9341_draw_boxes_only(const float *boxes, int nboxes,
                             uint16_t bg_color565, uint16_t box_color565) {
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set(GPIO_DC, 1);
    uint8_t buf[512];
    int bp = 0;
    uint8_t bg_hi = bg_color565 >> 8, bg_lo = bg_color565 & 0xFF;
    uint8_t bx_hi = box_color565 >> 8, bx_lo = box_color565 & 0xFF;

    for (int dy = 0; dy < LCD_H; dy++) {
        for (int dx = 0; dx < LCD_W; dx++) {
            int on_box = 0;
            for (int b = 0; b < nboxes && !on_box; b++) {
                int bx0 = (int)(boxes[b*4+0] * LCD_W);
                int by0 = (int)(boxes[b*4+1] * LCD_H);
                int bx1 = (int)(boxes[b*4+2] * LCD_W);
                int by1 = (int)(boxes[b*4+3] * LCD_H);
                if (bx0 < 0) bx0 = 0; if (bx1 >= LCD_W) bx1 = LCD_W - 1;
                if (by0 < 0) by0 = 0; if (by1 >= LCD_H) by1 = LCD_H - 1;
                int on_h = (dy >= by0 && dy <= by0+2) || (dy >= by1-2 && dy <= by1);
                int on_v = (dx >= bx0 && dx <= bx0+2) || (dx >= bx1-2 && dx <= bx1);
                int in_x = (dx >= bx0 && dx <= bx1);
                int in_y = (dy >= by0 && dy <= by1);
                on_box = (on_h && in_x) || (on_v && in_y);
            }
            buf[bp++] = on_box ? bx_hi : bg_hi;
            buf[bp++] = on_box ? bx_lo : bg_lo;
            if (bp == 512) { spi_send(buf, 512); bp = 0; }
        }
    }
    if (bp > 0) spi_send(buf, bp);
}

/* Draw NV21 frame with bounding boxes overlaid.
 * boxes: array of {x0,y0,x1,y1} normalized [0..1] relative to w,h.
 * nboxes: number of boxes. box_color565: RGB565 color for box edges. */
void ili9341_draw_nv21_boxes(const uint8_t *nv21, int w, int h, int stride,
                             const float *boxes, int nboxes, uint16_t box_color565) {
    const uint8_t *y_plane  = nv21;
    const uint8_t *vu_plane = nv21 + stride * h;

    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set(GPIO_DC, 1);
    uint8_t buf[512];
    int bp = 0;

    for (int dy = 0; dy < LCD_H; dy++) {
        int sy = dy * h / LCD_H;
        for (int dx = 0; dx < LCD_W; dx++) {
            int sx = dx * w / LCD_W;

            /* Check if pixel is on any box edge (3px thick) */
            int on_box = 0;
            for (int b = 0; b < nboxes && !on_box; b++) {
                int bx0 = (int)(boxes[b*4+0] * LCD_W);
                int by0 = (int)(boxes[b*4+1] * LCD_H);
                int bx1 = (int)(boxes[b*4+2] * LCD_W);
                int by1 = (int)(boxes[b*4+3] * LCD_H);
                if (bx0 < 0) bx0 = 0; if (bx1 >= LCD_W) bx1 = LCD_W - 1;
                if (by0 < 0) by0 = 0; if (by1 >= LCD_H) by1 = LCD_H - 1;
                int on_h = (dy >= by0 && dy <= by0+2) || (dy >= by1-2 && dy <= by1);
                int on_v = (dx >= bx0 && dx <= bx0+2) || (dx >= bx1-2 && dx <= bx1);
                int in_x = (dx >= bx0 && dx <= bx1);
                int in_y = (dy >= by0 && dy <= by1);
                on_box = (on_h && in_x) || (on_v && in_y);
            }

            uint16_t c;
            if (on_box) {
                c = box_color565;
            } else {
                uint8_t yv = y_plane[sy * stride + sx];
                int uv_off = (sy / 2) * stride + (sx & ~1);
                int8_t vv = (int8_t)(vu_plane[uv_off]     - 128);
                int8_t uv = (int8_t)(vu_plane[uv_off + 1] - 128);
                c = ili9341_yuv_to_565(yv, uv, vv);
            }
            buf[bp++] = c >> 8;
            buf[bp++] = c & 0xFF;
            if (bp == 512) { spi_send(buf, 512); bp = 0; }
        }
    }
    if (bp > 0) spi_send(buf, bp);
}

/* Overlay colored boxes onto the existing s_fb (no NV21 re-render),
 * then flush s_fb to LCD via SPI.
 * Call AFTER ili9341_draw_nv21_with_boxes_kpts to add a second layer
 * of boxes (e.g. face detection) on top without re-rendering the frame. */
void ili9341_overlay_boxes(const float *boxes, int nboxes, uint16_t color565) {
    if (nboxes <= 0) return;
    uint8_t bhi = color565 >> 8, blo = color565 & 0xFF;
    for (int b = 0; b < nboxes; b++) {
        int bx0 = (int)(boxes[b*4+0] * LCD_W); if (bx0 < 0) bx0 = 0;
        int by0 = (int)(boxes[b*4+1] * LCD_H); if (by0 < 0) by0 = 0;
        int bx1 = (int)(boxes[b*4+2] * LCD_W); if (bx1 >= LCD_W) bx1 = LCD_W - 1;
        int by1 = (int)(boxes[b*4+3] * LCD_H); if (by1 >= LCD_H) by1 = LCD_H - 1;
        for (int x = bx0; x <= bx1; x++) {
            uint8_t *t  = (uint8_t *)&s_fb[by0 * LCD_W + x]; t[0]  = bhi; t[1]  = blo;
            uint8_t *bt = (uint8_t *)&s_fb[by1 * LCD_W + x]; bt[0] = bhi; bt[1] = blo;
        }
        for (int y = by0; y <= by1; y++) {
            uint8_t *l = (uint8_t *)&s_fb[y * LCD_W + bx0]; l[0] = bhi; l[1] = blo;
            uint8_t *r = (uint8_t *)&s_fb[y * LCD_W + bx1]; r[0] = bhi; r[1] = blo;
        }
    }
    /* Flush updated framebuffer to LCD */
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    gpio_set(GPIO_DC, 1);
    const uint8_t *p = (const uint8_t *)s_fb;
    int rem = LCD_W * LCD_H * 2;
    while (rem > 0) {
        int n = rem > 4096 ? 4096 : rem;
        spi_send(p, n);
        p += n; rem -= n;
    }
}
