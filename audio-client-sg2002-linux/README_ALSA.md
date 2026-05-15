# Audio Wakeword Linux (SG2002 / Milk-V Duo256M)

Portable C++ TFLite Micro wakeword detector với MicroFrontend (Google), chạy trên **SG2002 big-core Linux**.

## Phiên bản

- **`audio_wakeword_linux`** (WAV reader) — Dùng trên PC/WSL để test với file .wav
- **`audio_wakeword_alsa`** (ALSA I2S) — Dùng trên Duo256M để capture từ INMP441 qua I2S

## Hardware Setup

### INMP441 6-pin Breakout → Milk-V Duo256M

| INMP441 | Chân | → Duo256M | Chân |
|---------|------|---------|------|
| VCC | 1 | 3V3(OUT) | 36 |
| GND | 2 | GND | 38 |
| SCK | 3 | GP6 | 9 |
| WS/LR | 4 | GP7 | 10 |
| L/R | 5 | GND | 38 (hoặc GP9 pin 12 để điều khiển) |
| SD | 6 | GP8 | 11 |

**Giải thích chân L/R:**
- Nối vào **GND** (pin 38): chỉ thu kênh trái (LEFT) — khuyến khích cho mono capture
- Nối vào **GP9** (pin 12): có thể toggle phần mềm để switch sang RIGHT nếu cần

## Build trên Linux (WSL)

### Yêu cầu

```bash
apt-get install cmake build-essential
# Để build ALSA version: apt-get install libasound2-dev
```

### Build WAV version (không cần ALSA)

```bash
cd audio-client-sg2002-linux
cmake -S . -B build
cmake --build build -j4
```

Output: `build/audio_wakeword_linux`

### Test WAV trên Linux/WSL

```bash
./build/audio_wakeword_linux --wav /path/to/16k_mono.wav --threshold 0.995
```

Tham số:
- `--wav FILE`: Đường dẫn file WAV (16 kHz, mono, 16-bit)
- `--threshold 0-1`: Ngưỡng detection (mặc định 0.995, thấp hơn = nhạy hơn)

## Build & Run trên Duo256M

### Yêu cầu trên board

```bash
opkg update
opkg install libasound2 libasound2-dev alsa-utils

# Hoặc compile từ source kernel đã có ALSA sẵn (chuẩn)
```

### Cross-compile cho riscv64-musl (từ PC)

Nếu có RISC-V cross-compiler:

```bash
cmake -S audio-client-sg2002-linux -B build-riscv \
  -DCMAKE_C_COMPILER=riscv64-linux-musl-gcc \
  -DCMAKE_CXX_COMPILER=riscv64-linux-musl-g++ \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-riscv -j4
```

Transfer về board:

```bash
scp build-riscv/audio_wakeword_alsa root@192.168.42.1:/root/
scp build-riscv/audio_wakeword_linux root@192.168.42.1:/root/
```

### Hoặc build trực tiếp trên board (Ubuntu image của Duo)

```bash
# SSH vào board
ssh root@192.168.42.1

# Cài build tools
apt-get update
apt-get install -y cmake build-essential libasound2-dev

# Clone/copy source
# (Or git clone từ repo)

cd audio-client-sg2002-linux
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2  # j2 vì resource hạn chế

# Build xong:
./build/audio_wakeword_alsa --help
```

## Chạy trên Duo256M

### 1. Kiểm tra I2S và ALSA

```bash
# Xem kernel log
dmesg | grep -i "i2s\|alsa"

# List capture devices
arecord -l

# Test ghi âm từ INMP441
arecord -D hw:0,0 -r 16000 -f S16_LE -c 1 -d 3 test.wav
aplay test.wav  # nghe lại
```

### 2. Chạy Wakeword detector

```bash
# Từ I2S INMP441
./audio_wakeword_alsa --device hw:0,0 --threshold 0.995 --duration 60

# Hoặc test từ file WAV
./audio_wakeword_linux --wav test.wav --threshold 0.995
```

**Output mẫu:**

```
=== INMP441 I2S Wakeword Detector (ALSA) ===
Device: hw:0,0
Threshold: 0.995
Listening... (Press Ctrl+C to stop)
Waiting for wakeword...
[0s] Confidence: 0.12
[1s] Confidence: 0.15
...
[15s] WAKEWORD DETECTED! Confidence: 0.997
...
```

## Troubleshooting

### Error: ALSA device "hw:0,0" not found

```bash
# Kiểm tra device có thể
arecord -l
arecord -L

# Nếu không thấy card I2S, cần update Device Tree với INMP441 codec
# (Xem hướng dẫn tạo DTS overlay - phức tạp hơn)
```

### Không nghe thấy tiếng

- Kiểm tra chân cắm: GP6/7/8 có nối chứa? L/R có nối GND không?
- Kiểm tra xung clock: `dmesg | tail` có lỗi I2S không?
- Thử `arecord -vv`: xem có dữ liệu không?

### Nhiễu quá cao

- Dùng dây jumper ngắn (<5cm)
- Kiểm tra GND chung giữa mic và board
- Giảm threshold (ví dụ 0.98 thay vì 0.995)

## Mô hình wakeword

File model: Loopy, chất lượng cao, chạy trên TFLite Micro.

Cấu trúc:
- Input: Log-Mel Spectrogram (40 bins, 30ms window, 10ms step)
- Quantization: INT8 (định lượng để chạy nhanh)
- Output: Confidence [0-1] cho từ khóa "Loopy"

## Tham khảo

- [TensorFlow Lite Micro](https://github.com/tensorflow/tflite-micro)
- [Google MicroFrontend](https://github.com/google/audio-io-windows/tree/main/tensorflow/lite/experimental/microfrontend)
- [INMP441 Datasheet](https://invensense.tdk.com/products/inmp441-low-power-digital-mems-microphone/)
- [Milk-V Duo Docs](https://milkv.io/docs/duo/getting-started/duo256m)

---

**Cập nhật:** May 2026
