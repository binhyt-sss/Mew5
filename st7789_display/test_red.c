#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

#define DC  503
#define RST 504

void gpio_export(int g) {
    char b[32]; int f = open("/sys/class/gpio/export", O_WRONLY);
    snprintf(b,32,"%d",g); write(f,b,strlen(b)); close(f);
}
void gpio_dir(int g, int out) {
    char p[64]; snprintf(p,64,"/sys/class/gpio/gpio%d/direction",g);
    int f = open(p,O_WRONLY); if(f<0){gpio_export(g);f=open(p,O_WRONLY);}
    write(f,out?"out":"in",out?3:2); close(f);
}
void gpio_set(int g, int v) {
    char p[64]; snprintf(p,64,"/sys/class/gpio/gpio%d/value",g);
    int f = open(p,O_WRONLY); write(f,v?"1":"0",1); close(f);
}
void spi_send(int fd, uint8_t *buf, int len) {
    struct spi_ioc_transfer t = {
        .tx_buf=(unsigned long)buf,
        .rx_buf=0,
        .len=len,
        .speed_hz=10000000,
        .bits_per_word=8
    };
    ioctl(fd, SPI_IOC_MESSAGE(1), &t);
}
void cmd(int fd, uint8_t c) { gpio_set(DC,0); spi_send(fd,&c,1); }
void dat1(int fd, uint8_t d) { gpio_set(DC,1); spi_send(fd,&d,1); }

int main() {
    printf("Setting up GPIO DC=%d RST=%d\n", DC, RST);
    gpio_dir(DC,1);
    gpio_dir(RST,1);

    printf("Hard reset...\n");
    gpio_set(RST,1); usleep(10000);
    gpio_set(RST,0); usleep(50000);
    gpio_set(RST,1); usleep(150000);

    int fd = open("/dev/spidev0.0", O_RDWR);
    if(fd<0){ perror("open spidev0.0"); return 1; }
    printf("SPI opened OK\n");

    uint8_t mode=SPI_MODE_0, bits=8;
    uint32_t spd=10000000;
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &spd);

    printf("Init ST7789...\n");
    cmd(fd,0x01); usleep(150000); /* SWRESET */
    cmd(fd,0x11); usleep(500000); /* SLPOUT  */
    cmd(fd,0x3A); dat1(fd,0x55); usleep(10000); /* COLMOD 16bit */
    cmd(fd,0x21); usleep(10000); /* INVON  */
    cmd(fd,0x13); usleep(10000); /* NORON  */
    cmd(fd,0x29); usleep(10000); /* DISPON */
    printf("Init done\n");

    /* Set window full screen: X=34..205 (172px), Y=0..319 */
    printf("Filling RED...\n");
    cmd(fd,0x2A);
    dat1(fd,0x00); dat1(fd,34);
    dat1(fd,0x00); dat1(fd,34+171);

    cmd(fd,0x2B);
    dat1(fd,0x00); dat1(fd,0x00);
    dat1(fd,0x01); dat1(fd,0x3F);

    cmd(fd,0x2C);
    gpio_set(DC,1);

    /* RED = 0xF800 in RGB565, big-endian: 0xF8 0x00 */
    uint8_t chunk[256];
    for(int i=0;i<256;i+=2){ chunk[i]=0xF8; chunk[i+1]=0x00; }
    int total = 172*320*2;
    int sent  = 0;
    while(sent < total) {
        int n = (total-sent) > 256 ? 256 : (total-sent);
        spi_send(fd, chunk, n);
        sent += n;
    }

    printf("Done! Screen should be RED now.\n");
    close(fd);
    return 0;
}
