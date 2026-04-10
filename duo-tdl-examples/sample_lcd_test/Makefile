TARGET=sample_lcd_test
TARGET2=lcd_color_test

ifeq (,$(COMMON_DIR))
$(error COMMON_DIR is not set)
endif

ifeq (,$(TOOLCHAIN_PREFIX))
$(error TOOLCHAIN_PREFIX is not set)
endif

CC ?= $(TOOLCHAIN_PREFIX)gcc

ifeq (,$(CFLAGS))
$(error CFLAGS is not set)
endif

ifeq (,$(LDFLAGS))
$(error LDFLAGS is not set)
endif

ifeq ($(CHIP), CV181X)
CFLAGS += -DCV181X -D__CV181X__
LDFLAGS += -lcvi_ive
else ifeq ($(CHIP), CV180X)
CFLAGS += -DCV180X -D__CV180X__
CFLAGS += -DUSE_TPU_IVE
LDFLAGS += -lcvi_ive_tpu
else
$(error CHIP is not set)
endif

CFLAGS += -std=gnu11 -Wno-pointer-to-int-cast -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s -lpthread -latomic

LDFLAGS += -lini -lsns_full -lsample -lteaisp -lisp -lvdec -lvenc -lawb -lae -laf -lcvi_bin -lcvi_bin_isp -lmisc -lisp_algo -lsys -lvi -lvo -lvpss -lrgn -lgdc -ldl
LDFLAGS += -lcvikernel -lcvimath -lcviruntime
LDFLAGS += -lcvi_tdl -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
LDFLAGS += -lcvi_rtsp

LCD_DIR ?= $(abspath $(COMMON_DIR)/../../st7789_display)
LCD_SRC = $(LCD_DIR)/st7789_lib.c
LCD_OBJ = $(LCD_SRC:.c=.o)
CFLAGS += -I$(LCD_DIR)

COMMON_SRC = $(COMMON_DIR)/middleware_utils.c $(COMMON_DIR)/sample_utils.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

OBJS = sample_lcd_test.o lcd_color_test.o

all: $(TARGET) $(TARGET2)

$(TARGET): sample_lcd_test.o $(COMMON_OBJ) $(LCD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(COMMON_OBJ) $(LCD_OBJ) sample_lcd_test.o $(LDFLAGS)

$(TARGET2): lcd_color_test.o $(LCD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(LCD_OBJ) lcd_color_test.o

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	@rm -rf *.o $(COMMON_OBJ) $(LCD_OBJ) $(OBJS) $(TARGET)
