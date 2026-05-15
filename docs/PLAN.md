# Plan: Cute Face Animation on ILI9341 LCD

## Context
Thêm animation engine hiển thị mặt cute trên ILI9341 (320×240 RGB565).
Mắt theo hướng bàn tay (wrist→middle MCP vector), expression thay đổi theo gesture label.
ION leak và ST7789 fbtft **là TODO - không làm phiên này**.

> **⚠️ Quy tắc cập nhật:**
> - Sau mỗi task hoàn thành -> tick `[x]` trong `D:\Mew5\docs\PLAN.md` và commit:
>   `git add -A && git commit -m "face-anim: <mô tả ngắn>"`
> - Nếu dừng giữa chừng -> ghi note vào mục **Status** trước khi thoát.

---

## Bước 0 - Tạo docs/PLAN.md (làm đầu tiên)
Tạo `D:\Mew5\docs\PLAN.md` với nội dung plan + progress checklist này.
File này là nguồn sự thật duy nhất cho tiến trình phiên này.

---

## Progress Checklist

### Phase 1 - Animation Library
- [x] **1.1** Tạo `st7789_display/face_anim.h` - `face_anim_t`, `face_expr_t`, API
- [x] **1.2** Tạo `st7789_display/face_anim.c` - `face_anim_init()`, `face_anim_update()`, lerp + blink
- [x] **1.3** Thêm primitives vào `st7789_display/ili9341_lib.c`: `fb_fill_circle`, `fb_fill_ellipse`, `fb_hline`, `fb_arc`, `fb_eyebrow`
- [x] **1.4** Thêm `ili9341_render_face(const face_anim_t *f)` vào `ili9341_lib.c` + khai báo `ili9341.h`

### Phase 2 - Tích hợp vào sample
- [x] **2.1** Thêm globals `g_lcd_gesture`, `g_lcd_palm_dx/dy` vào `sample_vi_hand_gesture.c`
- [x] **2.2** TDL thread: tính palm direction từ `kpts.x[9]-x[0]`, `kpts.y[9]-y[0]`
- [x] **2.3** LCD thread: thay NV21 render -> `face_anim_update` + `ili9341_render_face`
- [x] **2.4** `Makefile`: thêm `face_anim.c`, include `st7789_display/`

### Phase 3 - Build & Deploy
- [ ] **3.1** Build: `bash build_ubuntu2204_hand.sh` không error
- [ ] **3.2** Push: `sshpass -p milkv scp binary root@192.168.42.1:/mnt/data/`
> Note: Phase 3 build is currently blocked in this terminal because `bash` returns `E_ACCESSDENIED`.
> Run build again in WSL/Linux shell before step 3.2.

### Phase 4 - Kiểm thử
- [ ] **4.1** Không tay: idle + blink tự động
- [ ] **4.2** Đưa tay: mắt follow palm direction mượt
- [ ] **4.3** Từng gesture -> expression đúng
- [ ] **4.4** 3 lần chạy liên tiếp không crash

---

## Design Details

### face_anim_t
```c
typedef enum {
  EXPR_IDLE, EXPR_HAPPY, EXPR_ANGRY, EXPR_SURPRISED, EXPR_SAD,
  EXPR_WINK, EXPR_COOL, EXPR_CONTENT, EXPR_EXCITED, EXPR_CURIOUS
} face_expr_t;

typedef struct {
  float eye_x, eye_y;
  float eye_x_tgt, eye_y_tgt;
  face_expr_t expr, expr_tgt;
  float blink_t;   // countdown frames
  float eye_open;  // 1=mở 0=nhắm
} face_anim_t;
```

### Gesture -> Expression
| Gesture | Expression | Mắt | Miệng |
|---|---|---|---|
| -1 none | IDLE | round blink | line |
| 0 Open Hand | HAPPY | round | smile |
| 1 Fist | ANGRY | eyebrow V | frown |
| 2 Point Up | SURPRISED | wide | O |
| 3 Victory | WINK | trái squint | smile |
| 4 ThumbsUp | EXCITED | star | big smile |
| 5 ThumbsDown | SAD | droopy | frown |
| 6 OK | CONTENT | half-closed | slight smile |
| 7 Rock | COOL | squint | smirk |
| 8 Call | CURIOUS | side-eye | neutral |

### Face Layout (320×240)
```
face r=90 tại (160,120)
eyes: L(120,100) R(200,100)  r_white=25  r_pupil=12
mouth arc tại (160,148)
MAX_PUPIL_OFFSET = 13px
lerp factor = 0.18f/frame
blink: mỗi 60-120 frames
```

### Palm direction (pixels -> normalized)
```c
float dx = kp->x[9] - kp->x[0];
float dy = kp->y[9] - kp->y[0];
float len = sqrtf(dx*dx + dy*dy);
if (len > 0) { dx /= len; dy /= len; }
```

---

## Files sẽ thay đổi
| File | Hành động |
|---|---|
| `D:\Mew5\docs\PLAN.md` | NEW - file này |
| `st7789_display/face_anim.h` | NEW |
| `st7789_display/face_anim.c` | NEW |
| `st7789_display/ili9341_lib.c` | MODIFY - thêm primitives + render_face |
| `st7789_display/ili9341.h` | MODIFY - khai báo render_face |
| `sample_vi_hand_gesture/sample_vi_hand_gesture.c` | MODIFY - globals + LCD thread |
| `sample_vi_hand_gesture/Makefile` | MODIFY - thêm face_anim.c |

---

## TODO phiên sau
- [ ] ION leak: `build_middleware` -> deploy `libvi.so`
- [ ] ST7789 fbtft: fix DTS DC/RST -> `build_kernel` -> deploy `boot.sd`

---

## Status
- 2026-04-19: Completed Phase 1 and Phase 2 implementation in workspace.
- 2026-04-19: Tried `bash build_ubuntu2204_hand.sh` from this terminal, but bash failed with `E_ACCESSDENIED`.
- 2026-04-19: `docs/PLAN.md` updated with Phase 3 build-block note.
- Phase 3.2 and Phase 4 remain pending on target device.
