# Milk-V Duo 256M — Hand Gesture + ST7789 LCD

Hand gesture recognition on Milk-V Duo 256M (SG2002) with live camera feed displayed on a 1.47" ST7789 172×320 SPI LCD.

---

## Hardware

| Component | Model |
|-----------|-------|
| Board | Milk-V Duo 256M (SG2002) |
| Camera | GC2083 MIPI 1080P 30fps 10bit |
| LCD | 1.47" ST7789 172×320 SPI |

### ST7789 Wiring (Duo 256M → LCD)

| LCD Pin | Signal | Duo Pin | GPIO |
|---------|--------|---------|------|
| SCL / SCK | SPI2 CLK | Pin 9 (GP6) | — |
| SDA / MOSI | SPI2 MOSI | Pin 10 (GP7) | — |
| CS | SPI2 CS0 | Pin 11 (GP9) | — |
| DC | Data/Cmd | Pin 21 (GP16) | gpio496 (XGPIOA[16]) |
| RST | Reset | Pin 22 (GP17) | gpio497 (XGPIOA[17]) |
| VCC | 3.3 V | Pin 36 (3V3) | — |
| GND | Ground | Pin 38 (GND) | — |
| BLK | Backlight | Pin 36 (3V3) | — |

**SPI device**: `/dev/spidev1.0` (SPI2 on Duo 256M — NOT spidev0.0)  
**SPI mode**: MODE_0, 20 MHz  
**GPIO sysfs base**: XGPIOA base = 480, so GP16 = 496, GP17 = 497

---

## File Structure

```
d:\Mew5\
├── README.md                        (this file)
├── INVESTIGATION_LOG.md             (full debug log)
├── st7789_display\
│   ├── st7789.h                     (public API: LCD_W=172, LCD_H=320)
│   └── st7789_lib.c                 (SPI driver, GPIO sysfs, NV21→RGB565)
└── duo-tdl-examples\
    ├── common\
    │   ├── middleware_utils.c        (SAMPLE_TDL_Init_WM, VI/VPSS init)
    │   └── sample_utils.c
    ├── sample_vi_hand_gesture\
    │   ├── sample_vi_hand_gesture.c  (main app: camera → TDL inference + LCD)
    │   └── Makefile
    ├── sample_lcd_test\
    │   ├── sample_lcd_test.c         (camera → LCD only, no TDL)
    │   ├── lcd_color_test.c          (standalone color fill test)
    │   └── Makefile
    └── build_ubuntu2204_lcd_test.sh  (build script for lcd_test targets)
```

---

## Build (WSL Ubuntu-22.04)

### Hand gesture app

```bash
bash /mnt/d/Mew5/duo-tdl-examples/build_ubuntu2204_hand.sh
```

Output: `duo-tdl-examples/sample_vi_hand_gesture/sample_vi_hand_gesture`

### LCD test tools

```bash
bash /mnt/d/Mew5/duo-tdl-examples/build_ubuntu2204_lcd_test.sh
```

Output: `sample_lcd_test/sample_lcd_test` and `sample_lcd_test/lcd_color_test`

---

## Deploy

```bash
# Copy to board (USB RNDIS: 192.168.42.1, password: milkv)
sshpass -p milkv scp \
  duo-tdl-examples/sample_vi_hand_gesture/sample_vi_hand_gesture \
  root@192.168.42.1:/mnt/data/

sshpass -p milkv scp \
  duo-tdl-examples/sample_lcd_test/sample_lcd_test \
  duo-tdl-examples/sample_lcd_test/lcd_color_test \
  root@192.168.42.1:/mnt/data/

# Make executable
sshpass -p milkv ssh root@192.168.42.1 'chmod +x /mnt/data/sample_lcd_test /mnt/data/lcd_color_test /mnt/data/sample_vi_hand_gesture'
```

> `/tmp` on this board is `noexec` — use `/mnt/data/` instead.

---

## Run

### 1. Standalone LCD color test (verify wiring)

```bash
export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
/mnt/data/lcd_color_test
```

Cycles: WHITE → RED → GREEN → BLUE → BLACK (2 s each). No camera needed.

### 2. Camera → LCD (no TDL)

```bash
export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
/mnt/data/sample_lcd_test
```

### 3. Hand gesture + LCD

```bash
export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
/mnt/data/sample_vi_hand_gesture \
  /mnt/cvimodel/yolov8n_det_hand_384_640_INT8_cv181x.cvimodel \
  /mnt/cvimodel/keypoint_hand_128_128_INT8_cv181x.cvimodel \
  /mnt/cvimodel/cls_keypoint_hand_gesture_1_42_INT8_cv181x.cvimodel
```

### Factory reference binary

```bash
export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
/mnt/system/usr/bin/ai/sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel
```

### RTSP stream (when working)

```
rtsp://192.168.42.1/h264   (port 554)
```

---

## Environment Variables

| Variable | Effect |
|----------|--------|
| `SAMPLE_TDL_DISABLE_RTSP=1` | Skip RTSP server init |
| `SAMPLE_TDL_USE_PLAT_VPSS=1` | Use `SAMPLE_PLAT_VPSS_INIT` (factory path) |
| `SAMPLE_TDL_FORCE_MANUAL_VI_INIT=1` | Manual VI init sequence instead of `SAMPLE_PLAT_VI_INIT` |
| `SAMPLE_TDL_DISABLE_VENC=1` | Skip VENC/RTSP init entirely |

---

## Board Notes

- **SSH**: `ssh root@192.168.42.1` / password: `milkv`
- **Sensor config**: `/mnt/data/sensor_cfg.ini` — GC2083 I2C addr must be `sns_i2c_addr = 55` (0x37 hex)
- **Pin mux check**: `duo-pinmux -l` — GP16/GP17 must show `[v] GP16`, `[v] GP17` (GPIO mode)
- **Check SPI devices**: `ls /dev/spidev*` — expect `/dev/spidev1.0` for SPI2
- **Check GPIO numbers**: `cat /sys/kernel/debug/gpio` — XGPIOA base = 480

---

## Investigation Summary

Full log: [INVESTIGATION_LOG.md](INVESTIGATION_LOG.md)

### Root cause of `CVI_VPSS_GetChnFrame` → `0xc006800e`

The camera pipeline was never fully initialized: `SAMPLE_PLAT_VI_INIT` returned 0 (success) but MIPI was never started — confirmed by strace showing zero `0x6d` MIPI ioctls and no `/dev/cvi-mipi-rx` open, while the factory binary produces all of these.

Key finding: the factory binary's `SAMPLE_PLAT_VI_INIT` is compiled **statically into the binary** (not called from libsample.so), so its internal copy has different behavior than what our dynamically-linked version calls.

### What was ruled out

- Wrong sensor type (`enSnsType=26` for GC2083 is correct)
- Wrong I2C address (fixed `37 → 55`)
- Stale VPSS group state (pre-cleanup of grp 0 and 1 confirmed not sufficient)
- VENC/RTSP interference (disabling VENC doesn't fix `0xc006800e`)
- Dual-channel contention (single-channel probe still fails)
- VPSS device / input profile selection (`vpssDev=0/1`, `dual_isp/mem_isp/dual_mem` all same result)

### Fixes applied to get the app running

| Fix | File | Description |
|-----|------|-------------|
| NULL VirAddr segfault | `sample_vi_hand_gesture.c` | Use `CVI_SYS_Mmap(u64PhyAddr[0])` to access frame pixels |
| LCD blocks TDL thread | `sample_vi_hand_gesture.c` | Separate `run_lcd_thread()` with mutex + condvar |
| CHN0 deadlock | `sample_vi_hand_gesture.c` | venc thread idles when sharing channel with TDL |
| `c0010107` TDL fail | `middleware_utils.c` | Pre-cleanup both VPSS grp 0 and grp 1 (TDL uses grp 1 internally) |
| Wrong SPI device | `st7789_lib.c` | Changed `spidev0.0` → `spidev1.0` (SPI2 on Duo 256M) |
| Wrong GPIO numbers | `st7789_lib.c` | Changed 503/504 → 496/497 (GP16/GP17 = XGPIOA base 480 + 16/17) |
| SPI mode wrong | `st7789_lib.c` | Changed MODE_3 → MODE_0 for ST7789 |

### ST7789 display still not showing color

Despite `ST7789 init OK (172x320) MODE0` printing successfully:
- GPIO 496/497 export and set correctly via sysfs
- `duo-pinmux` confirms GP16/GP17 in GPIO mode
- Backlight is on (LCD panel powers up)
- RST pin toggle produces no visible screen change

Suspected cause: possible physical wiring issue or incorrect pin assignment for DC/RST. Verify with oscilloscope or logic analyzer on SCK/MOSI/CS lines during `lcd_color_test`.


# Build guild 
.FINAL_BUILD_GUIDE.md