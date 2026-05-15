#pragma once
#ifdef __cplusplus

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

extern "C" {
#endif

/**
 * Wakeword detector C interface – "loopy" MicroWakeWord streaming TFLite model.
 *
 * Matches kws_live.py / microwakeword_inference.py pipeline:
 *   Feature extractor : TFLite MicroFrontend  (window=480, step=160, 40 bins)
 *   Model input       : [1, 1, 40]  int8   scale=0.10196079  zp=-128
 *   Model output      : [1, 1]      uint8  scale=0.00390625  zp=0
 *   Voter             : window=30 frames (300 ms), votes_needed=20, cooldown=200 frames (2 s)
 */

/** One-time initialisation. Returns 0 on success. */
int  wakeword_init(float threshold);

/**
 * Push 16-kHz mono int16 PCM samples.
 * Internally extracts MicroFrontend frames (every 160 samples = 10 ms)
 * and runs TFLite inference on each.
 * Returns true when the voter fires (wakeword confirmed).
 */
bool wakeword_feed(const int16_t *samples, int n_samples);

/** Reset model state, frontend state, and voter (call after detection). */
void wakeword_reset(void);

#ifdef __cplusplus
}  // extern "C"
#endif
