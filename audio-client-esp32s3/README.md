# ESP32-S3 Audio Client (ESP-SR VADNet + TFLite Loopy Wakeword)

Firmware tích hợp pipeline xử lý âm thanh chuyên sâu cho ESP32-S3, kết hợp giữa chuẩn công nghiệp ESP-SR và trí tuệ nhân tạo TFLite Micro.

## 🚀 Luồng xử lý (Pipeline)

1.  **Thu âm (I2S DMA):** Microphone INMP441 thu âm 16kHz/16-bit.
2.  **Tiền xử lý (ESP-SR AFE):** Lọc nhiễu, khử echo và thực hiện Voice Activity Detection (VADNet).
3.  **Nhận diện Wakeword (TFLite Micro):** 
    *   Sử dụng model "Loopy" dạng **Streaming Stateful** (có bộ nhớ).
    *   Tự động "cộng dồn" Confidence qua từng frame 30ms để đạt độ chính xác cao nhất.
4.  **Phản hồi (OLED RLE):** Kích hoạt hoạt ảnh (Animation) từ dữ liệu RLE nén khi nhận diện thành công.
5.  **Streaming:** Chỉ gửi dữ liệu âm thanh chứa tiếng người (Speech) lên Server qua WebSocket.

## 🛠 Cấu hình & Build

### 1) Yêu cầu
- Board ESP32-S3 (Khuyến khích có PSRAM).
- PlatformIO CLI.

### 2) Lệnh thực thi
```bash
pio run -t upload      # Nạp chương trình
pio device monitor     # Xem logs và kết quả nhận diện
```

### 3) Cấu hình quan trọng (platformio.ini)
- `WIFI_SSID/PASS`: Thông tin mạng.
- `SERVER_URL`: Địa chỉ WebSocket Server.
- `OLED_SDA/SCL`: Chân kết nối màn hình (Mặc định GPIO 8, 9).

## 🧠 Đặc tính kỹ thuật nổi bật (Deep Dive)

Dựa trên việc phân tích mã nguồn, hệ thống sử dụng một quy trình tinh xảo từ sóng âm thô đến quyết định kích hoạt của AI, chia làm 4 giai đoạn chính:

### Giai đoạn 1: Thu âm và Chuẩn hóa (Hardware Layer)
Ngay khi bật nguồn, phần cứng I2S bắt đầu làm việc:
- **I2S DMA Capture:** Dữ liệu được thu ở tần số 16kHz, độ phân giải 32-bit (thực tế mic INMP441 trả về 24-bit đặt trong slot 32-bit).
- **Bit-depth Conversion:** Chip thực hiện dịch bit (`>> 16`) để đưa về chuẩn **16-bit PCM Mono**.
- **Software Gain Boost:** Hệ thống áp dụng bộ khuếch đại phần mềm (`WW_INPUT_GAIN = 4`). Việc nhân 4 biên độ sóng âm giúp AI "nghe" rõ hơn trong môi trường xa, đảm bảo đặc trưng âm thanh không bị quá nhỏ khi đi vào bộ lọc.

### Giai đoạn 2: Trích xuất đặc trưng (Feature Extraction - DSP)
Chuyển đổi "âm thanh" thành "hình ảnh" (Log-Mel Spectrogram) qua thư viện **Google MicroFrontend**:
- **Cửa sổ trượt (Sliding Window):**
    - **Window (30ms - 480 samples):** Phân tích tần số trên một đoạn âm thanh đủ dài.
    - **Step (10ms - 160 samples):** Cứ mỗi 10ms lại trượt tới để lấy dữ liệu mới, tạo sự chồng lấp (Overlap) giúp dòng chảy âm thanh liên tục.
- **Bộ lọc Mel (40 bins):** Năng lượng âm thanh được gom vào 40 kênh tần số theo thang đo Mel (mô phỏng tai người).
- **PCAN (Per-Channel AGC):** Đây là kỹ thuật **Tự động kiểm soát độ lợi theo từng kênh**. Nó giúp triệt tiêu nhiễu nền tĩnh và cân bằng lại âm sắc, giúp AI hoạt động ổn định bất kể môi trường ồn ào.

### Giai đoạn 3: Định lượng và Truyền trạng thái (Quantization & Stateful)
Đây là bước đồng bộ hóa dữ liệu giữa phần cứng và bộ não AI:
- **Công thức định lượng chuẩn:** `int8_q = (Raw_Mel * 0.0390625 / 0.10196) - 128`.
- **Giải mã các con số khoa học:**
    - **`0.0390625` (1/25.6):** Hằng số nén dải động từ Log-Mel về miền số thực chuẩn. Đây là con số bắt buộc để khớp với thư viện `microwakeword` trong quá trình huấn luyện.
    - **`0.10196` (Scale):** Trích xuất từ Metadata của model. Nó quy định mỗi đơn vị số nguyên đại diện cho bao nhiêu đơn vị số thực.
    - **`-128` (Zero Point):** Điểm đáy của kiểu dữ liệu `int8`. Nó ánh xạ mức năng lượng thấp nhất (tĩnh lặng) vào giá trị `-128`.
- **Tác dụng:** Đảm bảo không xảy ra hiện tượng **Covariate Shift** (sai lệch phân phối dữ liệu), giúp AI trên ESP32 nhìn thấy dữ liệu y hệt như lúc được huấn luyện trên siêu máy tính.
- **State Persistence (MicroResourceVariables):** Model liên kết các lớp ẩn với vùng nhớ (`VAR_HANDLE`), cho phép AI "nhớ" các âm tiết trước đó để hiểu được tính tuần tự (ví dụ: "Loo" rồi mới đến "py").

### Giai đoạn 4: Bộ lọc bình chọn (Voter Mechanism - Smoothing)
AI không kích hoạt ngay khi thấy 1 frame giống từ khóa để tránh nhầm lẫn (False Trigger):
- **Sliding Window Voter:** Duy trì bộ đệm `s_vote_buf` gồm **100 frame gần nhất** (~1 giây).
- **Voting Rule:** Chỉ khi có ít nhất **25/100 frame** đạt điểm tin cậy trên ngưỡng (Threshold), hệ thống mới chốt hạ: "Đúng là chủ nhân đang gọi Loopy!".
- **Cooldown:** Sau khi phát hiện, hệ thống "nghỉ" 200 frame để tránh kích hoạt hoạt ảnh nhiều lần liên tục.

### Cơ chế Chống trễ (Anti-Latency Draining)
Để đảm bảo tính thời gian thực sau khi thực hiện các tác vụ nặng (như chạy hoạt ảnh):
- Hệ thống tự động **"xả sạch" (Drain)** bộ đệm I2S và StreamBuffer trong suốt quá trình animation.
- Loại bỏ hoàn toàn hiện tượng "âm thanh cũ" bị kẹt, giúp lần gọi tiếp theo luôn phản hồi ngay tức thì.

### Hoạt ảnh OLED (RLE Compression)
Sử dụng thuật toán nén RLE (Run-Length Encoding) để lưu trữ chuỗi ảnh động Loopy phức tạp trong bộ nhớ Flash hạn chế, giải nén và vẽ trực tiếp lên màn hình với tốc độ cao.

## 📂 Cấu trúc Partition
Dùng [partitions.csv](partitions.csv) tùy chỉnh:
- `factory`: Chứa app chính.
- `model`: Chứa các trọng số VADNet của ESP-SR.

---
**Ngày cập nhật:** 2026-05-11
