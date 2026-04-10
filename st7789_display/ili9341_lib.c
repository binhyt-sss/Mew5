#include "ili9341.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_DEV   "/dev/spidev0.0"
#define GPIO_DC   503   /* GP16 = Pin 21 */
#define GPIO_RST  504   /* GP17 = Pin 22 */

/* ---------- GPIO ---------- */
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
static void gpio_set(int g, int v) {
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
    lcd_cmd(0x36); lcd_dat1(0x48);
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

    ili9341_hw_init();
    printf("ILI9341 init OK (%dx%d)\n", LCD_W, LCD_H);
    return 0;
}

void ili9341_close(void) {
    if (spi_fd >= 0) { close(spi_fd); spi_fd = -1; }
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
    int r = y + (int)(1.402f * v);
    int g = y - (int)(0.344f * u) - (int)(0.714f * v);
    int b = y + (int)(1.772f * u);
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void ili9341_draw_nv21(const uint8_t *nv21, int w, int h, int stride) {
    /* NV21: Y plane (stride*h bytes) + VU interleaved (stride*h/2 bytes) */
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
            uint8_t yv = y_plane[sy * stride + sx];
            /* VU plane: each 2x2 block shares one VU pair */
            int uv_off = (sy / 2) * stride + (sx & ~1);
            int8_t vv = (int8_t)(vu_plane[uv_off]     - 128);
            int8_t uv = (int8_t)(vu_plane[uv_off + 1] - 128);
            uint16_t c = ili9341_yuv_to_565(yv, uv, vv);
            buf[bp++] = c >> 8;
            buf[bp++] = c & 0xFF;
            if (bp == 512) { spi_send(buf, 512); bp = 0; }
        }
    }
    if (bp > 0) spi_send(buf, bp);
}
