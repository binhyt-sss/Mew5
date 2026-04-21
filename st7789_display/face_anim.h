#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  EXPR_IDLE = 0,
  EXPR_HAPPY,
  EXPR_ANGRY,
  EXPR_SURPRISED,
  EXPR_SAD,
  EXPR_WINK,
  EXPR_COOL,
  EXPR_CONTENT,
  EXPR_EXCITED,
  EXPR_CURIOUS
} face_expr_t;

typedef struct {
  float eye_x;
  float eye_y;
  float eye_x_tgt;
  float eye_y_tgt;
  float eye_vx;       /* spring velocity x */
  float eye_vy;       /* spring velocity y */
  face_expr_t expr;
  face_expr_t expr_tgt;
  float blink_t;
  float eye_open;
} face_anim_t;

void face_anim_init(face_anim_t *f);
void face_anim_update(face_anim_t *f, int gesture_label, float palm_dx, float palm_dy);

#ifdef __cplusplus
}
#endif
