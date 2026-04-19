#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define WIDTH  172
#define HEIGHT 320
#define SIZE   (WIDTH * HEIGHT * 2)

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void show(int fd, uint16_t *buf, uint16_t color) {
    for (int i = 0; i < WIDTH * HEIGHT; i++)
        buf[i] = color;
    lseek(fd, 0, SEEK_SET);
    write(fd, buf, SIZE);
}

static void rect(uint16_t *buf, int x, int y, int w, int h, uint16_t color) {
    for (int row = y; row < y + h && row < HEIGHT; row++)
        for (int col = x; col < x + w && col < WIDTH; col++)
            buf[row * WIDTH + col] = color;
}

int main(void) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("open /dev/fb0"); return 1; }

    uint16_t *buf = malloc(SIZE);
    if (!buf) { perror("malloc"); return 1; }

    printf("RED\n");   show(fd, buf, rgb(255, 0, 0));   sleep(1);
    printf("GREEN\n"); show(fd, buf, rgb(0, 255, 0));   sleep(1);
    printf("BLUE\n");  show(fd, buf, rgb(0, 0, 255));   sleep(1);

    printf("STRIPES\n");
    memset(buf, 0, SIZE);
    rect(buf, 0,   0,  172, 80,  rgb(255, 0,   0));
    rect(buf, 0,   80, 172, 80,  rgb(0,   255, 0));
    rect(buf, 0,   160, 172, 80, rgb(0,   0,   255));
    rect(buf, 0,   240, 172, 80, rgb(255, 255, 0));
    lseek(fd, 0, SEEK_SET);
    write(fd, buf, SIZE);
    sleep(2);

    printf("WHITE\n"); show(fd, buf, 0xFFFF); sleep(1);
    printf("BLACK\n"); show(fd, buf, 0x0000);

    free(buf);
    close(fd);
    printf("Done!\n");
    return 0;
}
