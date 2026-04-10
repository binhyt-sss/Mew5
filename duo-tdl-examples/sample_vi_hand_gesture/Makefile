TARGET=sample_vi_hand_gesture

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

LDFLAGS += -lini -lsns_full -lsample -lteaisp -lisp -lvdec -lvenc -lawb -lae -laf -lcvi_bin -lcvi_bin_isp -lmisc -lisp_algo -lsys  -lvi -lvo -lvpss -lrgn -lgdc -ldl
LDFLAGS += -lcvi_tdl
LDFLAGS += -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
LDFLAGS += -lcvikernel -lcvimath -lcviruntime
LDFLAGS += -lcvi_rtsp

LCD_DIR ?= $(abspath $(COMMON_DIR)/../../st7789_display)
LCD_SRC = $(LCD_DIR)/ili9341_lib.c
LCD_OBJ = $(LCD_SRC:.c=.o)

CFLAGS += -I$(LCD_DIR)

COMMON_SRC = $(COMMON_DIR)/middleware_utils.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

SOURCE = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SOURCE))

$(TARGET): $(OBJS) $(COMMON_OBJ) $(LCD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(COMMON_OBJ) $(LCD_OBJ) $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	@rm -rf *.o
	@rm -rf $(COMMON_OBJ)
	@rm -rf $(LCD_OBJ)
	@rm -rf $(OBJS)
	@rm -rf $(TARGET)

