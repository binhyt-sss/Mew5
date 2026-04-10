#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>

#define SPI_DEV   "/dev/spidev0.0"
#define GPIO_DC   503   /* GP16 = Pin 21 */
#define GPIO_RST  504   /* GP17 = Pin 22 */

#define LCD_W 240
#define LCD_H 320

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

static void cmd(uint8_t c) { gpio_set(GPIO_DC, 0); spi_send(&c, 1); }
static void dat1(uint8_t d) { gpio_set(GPIO_DC, 1); spi_send(&d, 1); }

static void ili9341_init(void) {
    gpio_set(GPIO_RST, 1); usleep(10000);
    gpio_set(GPIO_RST, 0); usleep(20000);
    gpio_set(GPIO_RST, 1); usleep(150000);

    cmd(0x01); usleep(100000);

    cmd(0xCB); dat1(0x39); dat1(0x2C); dat1(0x00); dat1(0x34); dat1(0x02);
    cmd(0xCF); dat1(0x00); dat1(0xC1); dat1(0x30);
    cmd(0xE8); dat1(0x85); dat1(0x00); dat1(0x78);
    cmd(0xEA); dat1(0x00); dat1(0x00);
    cmd(0xED); dat1(0x64); dat1(0x03); dat1(0x12); dat1(0x81);
    cmd(0xF7); dat1(0x20);
    cmd(0xC0); dat1(0x23);
    cmd(0xC1); dat1(0x10);
    cmd(0xC5); dat1(0x3E); dat1(0x28);
    cmd(0xC7); dat1(0x86);
    cmd(0x36); dat1(0x48);
    cmd(0x3A); dat1(0x55);
    cmd(0xB1); dat1(0x00); dat1(0x18);
    cmd(0xB6); dat1(0x08); dat1(0x82); dat1(0x27);
    cmd(0xF2); dat1(0x00);
    cmd(0x26); dat1(0x01);
    cmd(0xE0);
    dat1(0x0F); dat1(0x31); dat1(0x2B); dat1(0x0C);
    dat1(0x0E); dat1(0x08); dat1(0x4E); dat1(0xF1);
    dat1(0x37); dat1(0x07); dat1(0x10); dat1(0x03);
    dat1(0x0E); dat1(0x09); dat1(0x00);
    cmd(0xE1);
    dat1(0x00); dat1(0x0E); dat1(0x14); dat1(0x03);
    dat1(0x11); dat1(0x07); dat1(0x31); dat1(0xC1);
    dat1(0x48); dat1(0x08); dat1(0x0F); dat1(0x0C);
    dat1(0x31); dat1(0x36); dat1(0x0F);
    cmd(0x11); usleep(120000);
    cmd(0x29);
    printf("ILI9341 init OK (%dx%d)\n", LCD_W, LCD_H);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    cmd(0x2A); dat1(x0>>8); dat1(x0&0xFF); dat1(x1>>8); dat1(x1&0xFF);
    cmd(0x2B); dat1(y0>>8); dat1(y0&0xFF); dat1(y1>>8); dat1(y1&0xFF);
    cmd(0x2C);
}

static void fill_color(uint16_t color) {
    set_window(0, 0, LCD_W-1, LCD_H-1);
    gpio_set(GPIO_DC, 1);
    uint8_t buf[256];
    for (int i = 0; i < 256; i += 2) { buf[i] = color>>8; buf[i+1] = color&0xFF; }
    int total = LCD_W * LCD_H * 2, sent = 0;
    while (sent < total) {
        int n = (total-sent) > 256 ? 256 : (total-sent);
        spi_send(buf, n);
        sent += n;
    }
}

int main(void) {
    gpio_dir(GPIO_DC,  1);
    gpio_dir(GPIO_RST, 1);

    spi_fd = open(SPI_DEV, O_RDWR);
    if (spi_fd < 0) { perror("open spi"); return 1; }

    uint8_t mode = SPI_MODE_0, bits = 8;
    uint32_t spd = 40000000;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spd);

    ili9341_init();

    printf("WHITE\n");  fill_color(0xFFFF); sleep(2);
    printf("RED\n");    fill_color(0xF800); sleep(2);
    printf("GREEN\n");  fill_color(0x07E0); sleep(2);
    printf("BLUE\n");   fill_color(0x001F); sleep(2);
    printf("BLACK\n");  fill_color(0x0000); sleep(2);

    close(spi_fd);
    printf("Done\n");
    return 0;
}
