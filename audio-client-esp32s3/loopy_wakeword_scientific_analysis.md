# BÁO CÁO PHÂN TÍCH KHOA HỌC: TỐI ƯU HÓA HỆ THỐNG NHẬN DIỆN WAKEWORD TRÊN EDGE (ESP32-S3)

## 1. Tổng quan hệ thống (Project Overview)
Hệ thống được thiết kế để nhận diện từ khóa kích hoạt (Wakeword) "Loopy" bằng công nghệ Học sâu (Deep Learning) chạy trực tiếp trên chip ESP32-S3. Mục tiêu cốt lõi là đạt được sự cân bằng giữa **Độ chính xác (Accuracy)**, **Công suất thấp (Low Power)** và quan trọng nhất là **Độ trễ bằng không (Zero Latency)**.

## 2. Sự kỳ diệu của Mạng nơ-ron Streaming Stateful

Trong lĩnh vực nhận diện âm thanh, có sự khác biệt khổng lồ giữa mạng nơ-ron truyền thống và mạng hỗ trợ Streaming Stateful (có trạng thái).

### 2.1. So sánh các kiến trúc
| Đặc điểm | Mạng Non-Streaming (One-shot) | Mạng Streaming Stateful (Của chúng ta) |
| :--- | :--- | :--- |
| **Cách thức** | Cần đủ 1 giây âm thanh mới bắt đầu đoán. | Đoán liên tục dựa trên từng mẩu 30ms. |
| **Độ trễ** | Rất cao (Thường > 1 giây). | Cực thấp (Gần như tức thời khi dứt lời). |
| **Tính liên tục** | Dễ bị lỗi nếu từ khóa nằm ở ranh giới giữa 2 cửa sổ. | Mượt mà, dòng chảy thông tin không bị ngắt quãng. |
| **Bộ nhớ** | Tốn nhiều RAM để lưu cả cục âm thanh lớn. | RAM thấp nhờ chỉ lưu "trạng thái nén" (State). |

### 2.2. Tại sao nó "Kỳ diệu"?
Mạng nơ-ron của chúng ta (sử dụng `VAR_HANDLE` và `MicroResourceVariables`) thực chất sở hữu một **"Trí nhớ ngắn hạn"**. 
*   Mỗi khi 30ms âm thanh đi vào, AI không chỉ nhìn vào 30ms đó, mà nó kết hợp với "ấn tượng" còn sót lại của các khung hình trước.
*   Điều này cho phép model hiểu được tính **tuần tự** của ngôn ngữ. Nó hiểu rằng âm "Loo-" phải đi trước âm "-py" thì mới gọi là "Loopy".
*   Sự "cộng dồn" Confidence chính là minh chứng cho việc AI đang dần lắp ghép các mảnh ghép âm thanh lại với nhau trong bộ nhớ của nó.

## 3. Tiền xử lý dữ liệu: Từ Sóng âm đến Quang phổ (DSP Pipeline)

Trước khi AI có thể "nghe", âm thanh thô phải được chuyển đổi thành một dạng hình ảnh mà mạng nơ-ron có thể hiểu được (Spectrogram).

### 3.1. Các thông số cấu hình cốt lõi
*   **Window Size (Cửa sổ - 30ms / 480 samples):**
    *   *Ý nghĩa:* Đây là độ dài của một đoạn âm thanh được băm ra để phân tích tần số. 
    *   *Tại sao 30ms?* Nếu quá ngắn, ta không đủ dữ liệu để biết tần số là gì. Nếu quá dài (như bạn đã nhận xét), các âm tiết nhanh của tiếng Việt sẽ bị "nhòe" (smearing). 30ms là con số tối ưu cho độ phân giải tần số trong nhận diện giọng nói tiếng Anh/Quốc tế.
*   **Stride (Bước nhảy - 10ms / 160 samples):**
    *   *Ý nghĩa:* Khoảng cách tịnh tiến giữa các cửa sổ. Chúng ta gối chồng các cửa sổ lên nhau (Overlap 66%).
    *   *Tác dụng:* Đảm bảo không có bất kỳ thông tin cực ngắn nào bị bỏ lỡ tại ranh giới các khung hình.
*   **Mel Filterbanks (40 bins):**
    *   *Ý nghĩa:* Sau khi phân tích tần số (FFT), ta gom các tần số vào 40 nhóm (bins) theo thang đo Mel.
    *   *Tại sao dùng Mel?* Tai người không nghe các tần số một cách tuyến tính. Chúng ta nhạy cảm hơn với các thay đổi ở tần số thấp và kém nhạy hơn ở tần số cao. Thang đo Mel mô phỏng lại chính xác cách màng nhĩ con người lọc âm thanh, giúp AI "nghe" giống người hơn.
*   **INT8 Quantization (Định lượng 8-bit):**
    *   *Ý nghĩa:* Chuyển đổi các trọng số AI từ số thực (Float32) sang số nguyên (Int8).
    *   *Tác dụng:* Giảm 4 lần dung lượng model và cho phép ESP32-S3 sử dụng tập lệnh **SIMD (Single Instruction, Multiple Data)** để tính toán song song, giúp tốc độ suy luận nhanh gấp hàng chục lần.

## 4. Triệt tiêu độ trễ phần cứng (Hardware Latency)

*   **Vấn đề:** Trong lúc ESP32 bận nháy đèn hoặc vẽ màn hình, phần cứng DMA vẫn liên tục nạp âm thanh vào bộ đệm. Đây gọi là "Âm thanh bóng ma" (Stale audio).
*   **Giải pháp "Safe Drain Loop":** 
    *   Chúng ta không dùng `i2s_zero_dma_buffer` vì nó phá vỡ cấu trúc bộ đệm RX. 
    *   Thay vào đó, ta sử dụng vòng lặp `i2s_read` thần tốc để "rút sạch rác" trong hàng đợi ngay sau khi xử lý xong sự kiện. Điều này đảm bảo khi hệ thống quay lại chế độ nghe, nó sẽ nghe thấy âm thanh của đúng giây phút đó.

## 5. Phân tích Voice Activity Detection (VAD)

Hệ thống sử dụng toán tử **RMS (Root Mean Square)** để tính năng lượng trung bình của sóng âm. 
*   **Công thức:** $RMS = \sqrt{\frac{1}{n} \sum_{i=1}^{n} x_i^2}$
*   **Ứng dụng:** Ngưỡng `1900` đóng vai trò như một người gác cổng. Nó giữ cho bộ não AI được nghỉ ngơi khi phòng yên tĩnh, chỉ kích hoạt năng lượng tính toán khi thực sự có tiếng người nói, tối ưu hóa nhiệt độ và tuổi thọ cho chip.

## 6. Kết luận
Dự án "Loopy Wakeword" là một sự kết hợp hoàn hảo giữa **Xử lý tín hiệu số (DSP)** và **Trí tuệ nhân tạo (TinyML)**. Việc hiểu rõ các thông số từ độ dài cửa sổ (Window) đến cách quản lý trạng thái (Stateful) chính là chìa khóa để tạo ra một thiết bị Edge AI đẳng cấp thế giới.

---
**Người thực hiện:** Antigravity AI & User
**Ngày cập nhật:** 2026-05-11
