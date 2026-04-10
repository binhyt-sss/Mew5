#include "st7789.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int spi_fd = -1;

/* ---------- GPIO helpers via sysfs ---------- */
static void gpio_export(int gpio) {
    char buf[64];
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return;
    snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, strlen(buf));
    close(fd);
}

static void gpio_set_direction(int gpio, int out) {
    char path[80];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) { gpio_export(gpio); fd = open(path, O_WRONLY); }
    if (fd < 0) return;
    write(fd, out ? "out" : "in", out ? 3 : 2);
    close(fd);
}

static void gpio_write(int gpio, int val) {
    char path[80];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return;
    write(fd, val ? "1" : "0", 1);
    close(fd);
}

/* ---------- SPI ---------- */
static void spi_write_bytes(const uint8_t *data, int len) {
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)data,
        .rx_buf = 0,
        .len    = len,
        .speed_hz = 40000000,
        .bits_per_word = 8,
    };
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

/* ---------- ST7789 low-level ---------- */
static void st7789_cmd(uint8_t cmd) {
    gpio_write(ST7789_GPIO_DC, 0);
    spi_write_bytes(&cmd, 1);
}

static void st7789_data(const uint8_t *data, int len) {
    gpio_write(ST7789_GPIO_DC, 1);
    spi_write_bytes(data, len);
}

static void st7789_data1(uint8_t d) {
    st7789_data(&d, 1);
}

static void st7789_reset(void) {
    gpio_write(ST7789_GPIO_RST, 1); usleep(10000);
    gpio_write(ST7789_GPIO_RST, 0); usleep(10000);
    gpio_write(ST7789_GPIO_RST, 1); usleep(120000);
}

static void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    /* 172x320 panel: X offset = 34, Y offset = 0 */
    x0 += 34; x1 += 34;
    uint8_t buf[4];

    st7789_cmd(ST7789_CASET);
    buf[0] = x0 >> 8; buf[1] = x0 & 0xFF;
    buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
    st7789_data(buf, 4);

    st7789_cmd(ST7789_RASET);
    buf[0] = y0 >> 8; buf[1] = y0 & 0xFF;
    buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
    st7789_data(buf, 4);

    st7789_cmd(ST7789_RAMWR);
}

/* ---------- Public API ---------- */
int st7789_init(void) {
    /* Setup GPIO */
    gpio_set_direction(ST7789_GPIO_DC,  1);
    gpio_set_direction(ST7789_GPIO_RST, 1);

    /* Open SPI */
    spi_fd = open(ST7789_SPI_DEV, O_RDWR);
    if (spi_fd < 0) {
        perror("open spidev2.0");
        return -1;
    }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = 40000000;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    /* Hardware reset */
    st7789_reset();

    /* Init sequence */
    st7789_cmd(ST7789_SWRESET); usleep(150000);
    st7789_cmd(ST7789_SLPOUT);  usleep(500000);

    st7789_cmd(ST7789_COLMOD);
    st7789_data1(ST7789_COLOR_MODE_16BIT);
    usleep(10000);

    st7789_cmd(ST7789_MADCTL);
    st7789_data1(0x00); /* portrait, RGB */

    st7789_cmd(ST7789_INVON);  /* this panel needs inversion ON */
    usleep(10000);
    st7789_cmd(ST7789_NORON);
    usleep(10000);
    st7789_cmd(ST7789_DISPON);
    usleep(10000);

    printf("ST7789 init OK (%dx%d)\n", ST7789_WIDTH, ST7789_HEIGHT);
    return 0;
}

void st7789_deinit(void) {
    if (spi_fd >= 0) { close(spi_fd); spi_fd = -1; }
}

void st7789_fill(uint16_t color) {
    st7789_set_window(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
    gpio_write(ST7789_GPIO_DC, 1);

    /* Send in 512-byte chunks */
    uint8_t buf[512];
    for (int i = 0; i < 512; i += 2) {
        buf[i]   = color >> 8;
        buf[i+1] = color & 0xFF;
    }
    int total = ST7789_WIDTH * ST7789_HEIGHT * 2;
    int sent  = 0;
    while (sent < total) {
        int chunk = (total - sent) > 512 ? 512 : (total - sent);
        spi_write_bytes(buf, chunk);
        sent += chunk;
    }
}

void st7789_draw_image(const uint16_t *pixels, uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h) {
    st7789_set_window(x, y, x + w - 1, y + h - 1);
    gpio_write(ST7789_GPIO_DC, 1);

    /* Swap bytes for big-endian SPI */
    uint8_t buf[512];
    int idx   = 0;
    int bufpos = 0;
    for (int i = 0; i < w * h; i++) {
        buf[bufpos++] = pixels[idx] >> 8;
        buf[bufpos++] = pixels[idx] & 0xFF;
        idx++;
        if (bufpos == 512) {
            spi_write_bytes(buf, 512);
            bufpos = 0;
        }
    }
    if (bufpos > 0) spi_write_bytes(buf, bufpos);
}
