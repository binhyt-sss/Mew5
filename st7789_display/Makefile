TARGET = st7789_show

TOOLCHAIN = /mnt/d/Mew5/duo-tdl-examples/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC = $(TOOLCHAIN)gcc

CFLAGS = -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d \
         -O2 -Wall -Wno-unused-result

SRCS = main.c st7789.c
OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
