#include "st7789.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_DEV   "/dev/spidev1.0"  /* SPI2 on Duo 256M */
#define GPIO_DC   496   /* GP16 = XGPIOA[16] = 480+16, Pin 21 */
#define GPIO_RST  497   /* GP17 = XGPIOA[17] = 480+17, Pin 22 */

/* ST7789 172x320: panel has a 34-column offset on the X axis */
#define COL_OFFSET  34
#define ROW_OFFSET  0

/* ---------- GPIO (sysfs) ---------- */
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
        .tx_buf        = (unsigned long)buf,
        .rx_buf        = 0,
        .len           = len,
        .speed_hz      = 20000000,
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

/* ---------- Window ---------- */
static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += COL_OFFSET;
    x1 += COL_OFFSET;
    y0 += ROW_OFFSET;
    y1 += ROW_OFFSET;

    lcd_cmd(0x2A);  /* CASET */
    lcd_dat1(x0 >> 8); lcd_dat1(x0 & 0xFF);
    lcd_dat1(x1 >> 8); lcd_dat1(x1 & 0xFF);

    lcd_cmd(0x2B);  /* RASET */
    lcd_dat1(y0 >> 8); lcd_dat1(y0 & 0xFF);
    lcd_dat1(y1 >> 8); lcd_dat1(y1 & 0xFF);

    lcd_cmd(0x2C);  /* RAMWR */
}

/* ---------- ST7789 init ---------- */
static void st7789_hw_init(void) {
    /* Hardware reset */
    gpio_set(GPIO_RST, 1); usleep(10000);
    gpio_set(GPIO_RST, 0); usleep(20000);
    gpio_set(GPIO_RST, 1); usleep(150000);

    lcd_cmd(0x01);           /* SWRESET */
    usleep(150000);

    lcd_cmd(0x11);           /* SLPOUT */
    usleep(120000);

    lcd_cmd(0x3A);           /* COLMOD: 16-bit color (RGB565) */
    lcd_dat1(0x55);

    lcd_cmd(0x36);           /* MADCTL: BGR, portrait */
    lcd_dat1(0x00);

    /* Porch setting */
    lcd_cmd(0xB2);
    lcd_dat1(0x0C); lcd_dat1(0x0C); lcd_dat1(0x00);
    lcd_dat1(0x33); lcd_dat1(0x33);

    lcd_cmd(0xB7);           /* Gate control */
    lcd_dat1(0x35);

    lcd_cmd(0xBB);           /* VCOM */
    lcd_dat1(0x19);

    lcd_cmd(0xC0);           /* LCM control */
    lcd_dat1(0x2C);

    lcd_cmd(0xC2);           /* VDV/VRH enable */
    lcd_dat1(0x01);

    lcd_cmd(0xC3);           /* VRH set */
    lcd_dat1(0x12);

    lcd_cmd(0xC4);           /* VDV set */
    lcd_dat1(0x20);

    lcd_cmd(0xC6);           /* Frame rate: 60Hz */
    lcd_dat1(0x0F);

    lcd_cmd(0xD0);           /* Power control 1 */
    lcd_dat1(0xA4); lcd_dat1(0xA1);

    /* Positive gamma */
    lcd_cmd(0xE0);
    lcd_dat1(0xD0); lcd_dat1(0x04); lcd_dat1(0x0D); lcd_dat1(0x11);
    lcd_dat1(0x13); lcd_dat1(0x2B); lcd_dat1(0x3F); lcd_dat1(0x54);
    lcd_dat1(0x4C); lcd_dat1(0x18); lcd_dat1(0x0D); lcd_dat1(0x0B);
    lcd_dat1(0x1F); lcd_dat1(0x23);

    /* Negative gamma */
    lcd_cmd(0xE1);
    lcd_dat1(0xD0); lcd_dat1(0x04); lcd_dat1(0x0C); lcd_dat1(0x11);
    lcd_dat1(0x13); lcd_dat1(0x2C); lcd_dat1(0x3F); lcd_dat1(0x44);
    lcd_dat1(0x51); lcd_dat1(0x2F); lcd_dat1(0x1F); lcd_dat1(0x1F);
    lcd_dat1(0x20); lcd_dat1(0x23);

    lcd_cmd(0x21);           /* INVON — required for correct colors on ST7789 */

    lcd_cmd(0x29);           /* DISPON */
    usleep(20000);
}

/* ---------- Public API ---------- */

int st7789_open(void) {
    gpio_dir(GPIO_DC,  1);
    gpio_dir(GPIO_RST, 1);

    spi_fd = open(SPI_DEV, O_RDWR);
    if (spi_fd < 0) { perror("st7789: open spi"); return -1; }

    uint8_t mode = SPI_MODE_0;  /* ST7789 works with MODE0 (CPOL=0 CPHA=0) */
    uint8_t bits = 8;
    uint32_t spd = 20000000;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spd);

    st7789_hw_init();
    printf("ST7789 init OK (%dx%d) MODE0 40MHz\n", LCD_W, LCD_H);
    return 0;
}

void st7789_close(void) {
    if (spi_fd >= 0) { close(spi_fd); spi_fd = -1; }
}

void st7789_fill(uint16_t color) {
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

static uint16_t yuv_to_565(uint8_t y, int8_t u, int8_t v) {
    int r = y + (int)(1.402f * v);
    int g = y - (int)(0.344f * u) - (int)(0.714f * v);
    int b = y + (int)(1.772f * u);
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void st7789_draw_nv21(const uint8_t *nv21, int w, int h, int stride) {
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
            int uv_off = (sy / 2) * stride + (sx & ~1);
            int8_t vv = (int8_t)(vu_plane[uv_off]     - 128);
            int8_t uv = (int8_t)(vu_plane[uv_off + 1] - 128);
            uint16_t c = yuv_to_565(yv, uv, vv);
            buf[bp++] = c >> 8;
            buf[bp++] = c & 0xFF;
            if (bp == 512) { spi_send(buf, 512); bp = 0; }
        }
    }
    if (bp > 0) spi_send(buf, bp);
}
