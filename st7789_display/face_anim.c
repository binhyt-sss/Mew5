#include "face_anim.h"

#include <stdlib.h>
#include <string.h>

#define FACE_SPRING_K        0.10f  /* spring stiffness — higher = snappier */
#define FACE_SPRING_DAMP     0.68f  /* < 1.0 = underdamped (bouncy) */
#define FACE_EYE_OPEN_LERP   0.30f
#define FACE_MAX_EYE_OFFSET  22.0f  /* larger = more expressive gaze */
#define FACE_BLINK_MIN_FRAMES 70
#define FACE_BLINK_MAX_FRAMES 130
#define FACE_BLINK_FRAMES     12

static float clampf_local(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static face_expr_t gesture_to_expr(int gesture_label) {
  switch (gesture_label) {
    case 0: return EXPR_HAPPY;
    case 1: return EXPR_ANGRY;
    case 2: return EXPR_SURPRISED;
    case 3: return EXPR_WINK;
    case 4: return EXPR_EXCITED;
    case 5: return EXPR_SAD;
    case 6: return EXPR_CONTENT;
    case 7: return EXPR_COOL;
    case 8: return EXPR_CURIOUS;
    default: return EXPR_IDLE;
  }
}

static float next_blink_countdown(void) {
  int jitter = rand() % (FACE_BLINK_MAX_FRAMES - FACE_BLINK_MIN_FRAMES + 1);
  return (float)(FACE_BLINK_MIN_FRAMES + jitter + FACE_BLINK_FRAMES);
}

void face_anim_init(face_anim_t *f) {
  if (f == NULL) return;
  memset(f, 0, sizeof(*f));
  f->expr = EXPR_IDLE;
  f->expr_tgt = EXPR_IDLE;
  f->eye_open = 1.0f;
  f->blink_t = next_blink_countdown();
}

void face_anim_update(face_anim_t *f, int gesture_label, float palm_dx, float palm_dy) {
  if (f == NULL) return;

  f->expr_tgt = gesture_to_expr(gesture_label);
  f->expr = f->expr_tgt;

  palm_dx = clampf_local(palm_dx, -1.0f, 1.0f);
  palm_dy = clampf_local(palm_dy, -1.0f, 1.0f);

  f->eye_x_tgt = palm_dx * FACE_MAX_EYE_OFFSET;
  f->eye_y_tgt = palm_dy * FACE_MAX_EYE_OFFSET;
  /* Spring physics: v += k*(target-pos);  v *= damp;  pos += v */
  f->eye_vx = (f->eye_vx + (f->eye_x_tgt - f->eye_x) * FACE_SPRING_K) * FACE_SPRING_DAMP;
  f->eye_vy = (f->eye_vy + (f->eye_y_tgt - f->eye_y) * FACE_SPRING_K) * FACE_SPRING_DAMP;
  f->eye_x += f->eye_vx;
  f->eye_y += f->eye_vy;

  if (f->blink_t <= 0.0f) {
    f->blink_t = next_blink_countdown();
  }
  f->blink_t -= 1.0f;

  float blink_open = 1.0f;
  if (f->blink_t <= FACE_BLINK_FRAMES) {
    float t = (FACE_BLINK_FRAMES - f->blink_t) / FACE_BLINK_FRAMES;
    if (t < 0.5f) {
      blink_open = 1.0f - (t * 2.0f);
    } else {
      blink_open = (t - 0.5f) * 2.0f;
    }
  }

  float expr_open = 1.0f;
  switch (f->expr) {
    case EXPR_CONTENT:   expr_open = 0.65f; break;
    case EXPR_COOL:      expr_open = 0.45f; break;
    case EXPR_SAD:       expr_open = 0.75f; break;
    case EXPR_SURPRISED: expr_open = 1.10f; break;
    default:             expr_open = 1.0f; break;
  }

  float eye_open_tgt = clampf_local(expr_open * blink_open, 0.05f, 1.20f);
  f->eye_open += (eye_open_tgt - f->eye_open) * FACE_EYE_OPEN_LERP;
}
