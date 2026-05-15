/**
 * wakeword.cc  –  "loopy" MicroWakeWord streaming TFLite Micro detector
 *
 * Feature pipeline identical to kws_live.py:
 *   MicroFrontend: window=480 samples (30 ms), step=160 samples (10 ms), 40
 * bins TFLite model:  input [1,1,40] int8 → output [1,1] uint8 Voter: window=30
 * frames, votes_needed=20, cooldown=200 frames
 */

#include "wakeword.h"
#include "loopy_model_microwakeword.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <cstdarg>
#include <cstdlib>
#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM 0
#endif
#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT 0
#endif
static inline void *heap_caps_malloc(size_t size, unsigned) {
  return std::malloc(size);
}
static inline void esp_log_print(const char *level, const char *tag,
                                 const char *format, ...) {
  va_list args;
  va_start(args, format);
  std::fprintf(stderr, "[%s] %s: ", level, tag);
  std::vfprintf(stderr, format, args);
  std::fprintf(stderr, "\n");
  va_end(args);
}
#define ESP_LOGE(tag, format, ...) esp_log_print("E", tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) esp_log_print("W", tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) esp_log_print("I", tag, format, ##__VA_ARGS__)
#else
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#endif

// TFLite Micro (from espressif/esp-tflite-micro managed component)
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

// MicroFrontend (sources in
// src/tensorflow/lite/experimental/microfrontend/lib/)
extern "C" {
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
}

static const char *TAG = "wakeword";

// ─── Model quantisation constants (from microwakeword_inference.py) ─────────
static constexpr float INPUT_SCALE = 0.10196079f;
static constexpr int INPUT_ZERO_POINT = -128;
// Verified Python uses 1/255 for this model's sigmoid output.
static constexpr float OUTPUT_SCALE = 1.0f / 255.0f;
static constexpr int OUTPUT_ZERO_POINT = 0;
// IMPORTANT: pymicro_features path converts uint16 frontend bins by *0.0390625
// before int8 quantization. Must match exactly or inputs collapse near -128.
static constexpr float FEAT_SCALE = 0.0390625f; // uint16 → float

// ─── Streaming voter (mirrors SmoothingVoter in kws_live.py) ─────────────────
static constexpr int VOTE_WINDOW = 100;
static constexpr int VOTES_NEEDED = 20;
static constexpr int VOTE_COOLDOWN = 200;
static constexpr int DEBUG_LOG_EVERY_FRAMES = 10; // 100 ms @ 10 ms/frame

// ─── TFLite Micro objects ────────────────────────────────────────────────────
static constexpr int ARENA_SIZE = 60 * 1024;

// Ops used by the loopy microwakeword model (12 total)
static tflite::MicroMutableOpResolver<12> s_resolver;

static uint8_t *s_arena = nullptr;
static tflite::MicroAllocator *s_allocator = nullptr;
static tflite::MicroResourceVariables *s_resource_vars = nullptr;
static tflite::MicroInterpreter *s_interpreter = nullptr;
static TfLiteTensor *s_input = nullptr;
static TfLiteTensor *s_output = nullptr;

// ─── MicroFrontend state ─────────────────────────────────────────────────────
static FrontendState s_fe_state;
static bool s_fe_ready = false;

// ─── Voter state ─────────────────────────────────────────────────────────────
static float s_vote_buf[VOTE_WINDOW];
static int s_vote_pos = 0;
static int s_vote_count = 0;
static int s_cooldown_left = 0;

// ─── Config ──────────────────────────────────────────────────────────────────
static float s_threshold = 0.50f;
static bool s_ready = false;

// Deterministic one-frame self-test vector (found with desktop TFLite).
// Expected raw output on reference interpreter: ~6 (non-zero).
static const int8_t kSelfTestQ[40] = {
    2,   89,   107, 35,  87, 2,  61,   -59, 104, -50, -67, -118, 112, -109,
    -89, -124, -47, -84, -6, 80, -100, 38,  -39, 105, 50,  0,    -5,  27,
    26,  120,  -55, 58,  -1, 33, -127, 11,  63,  15,  104, 111};

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void refresh_io_tensors() {
  if (!s_interpreter)
    return;
  s_input = s_interpreter->input(0);
  s_output = s_interpreter->output(0);
}

static void reset_tflite_state() {
  // For stream_state_internal models, only reset resource variables.
  // Avoid MicroInterpreter::Reset() because it can invalidate cached tensor
  // pointers.
  if (s_resource_vars) {
    s_resource_vars->ResetAll();
  }
  refresh_io_tensors();
}

static void reset_voter() {
  memset(s_vote_buf, 0, sizeof(s_vote_buf));
  s_vote_pos = 0;
  s_vote_count = 0;
  s_cooldown_left = 0;
}

static void run_self_test_once() {
  if (!s_interpreter || !s_input || !s_output)
    return;
  memcpy(s_input->data.int8, kSelfTestQ, sizeof(kSelfTestQ));
  if (s_interpreter->Invoke() != kTfLiteOk) {
    ESP_LOGW(TAG, "Self-test invoke failed");
    return;
  }
  int raw = (s_output->type == kTfLiteUInt8) ? (int)s_output->data.uint8[0]
                                             : (int)s_output->data.int8[0];
  ESP_LOGW(TAG, "Self-test raw=%d (reference ~6)", raw);
  // Restore clean streaming state after self-test.
  reset_tflite_state();
}

static bool run_inference(const uint16_t *features_u16) {
  refresh_io_tensors();
  if (!s_input || !s_output) {
    ESP_LOGW(TAG, "I/O tensor unavailable");
    return false;
  }
  // Quantise exactly like microwakeword_inference.py:
  //   float_feat = u16 * 0.0390625
  //   int8_q     = float_feat / INPUT_SCALE + INPUT_ZERO_POINT
  const float q_mul = FEAT_SCALE / INPUT_SCALE;
  int8_t *dst = s_input->data.int8;
  uint16_t feat_min = 65535;
  uint16_t feat_max = 0;
  int8_t q_min = 127;
  int8_t q_max = -128;
  for (int i = 0; i < 40; i++) {
    if (features_u16[i] < feat_min)
      feat_min = features_u16[i];
    if (features_u16[i] > feat_max)
      feat_max = features_u16[i];
    int32_t q =
        (int32_t)roundf((float)features_u16[i] * q_mul) + INPUT_ZERO_POINT;
    if (q < -128)
      q = -128;
    if (q > 127)
      q = 127;
    dst[i] = (int8_t)q;
    if (dst[i] < q_min)
      q_min = dst[i];
    if (dst[i] > q_max)
      q_max = dst[i];
  }

  if (s_interpreter->Invoke() != kTfLiteOk) {
    ESP_LOGW(TAG, "Invoke failed");
    return false;
  }

  float prob = 0.0f;
  if (s_output->type == kTfLiteUInt8) {
    prob = ((float)s_output->data.uint8[0] - OUTPUT_ZERO_POINT) * OUTPUT_SCALE;
  } else if (s_output->type == kTfLiteInt8) {
    prob = ((float)s_output->data.int8[0] - OUTPUT_ZERO_POINT) * OUTPUT_SCALE;
  } else {
    prob = s_output->data.f[0];
  }
  if (prob < 0.0f)
    prob = 0.0f;
  if (prob > 1.0f)
    prob = 1.0f;

  // Push to voter
  s_vote_buf[s_vote_pos] = prob;
  s_vote_pos = (s_vote_pos + 1) % VOTE_WINDOW;
  if (s_vote_count < VOTE_WINDOW)
    s_vote_count++;

  if (s_cooldown_left > 0) {
    s_cooldown_left--;
    return false;
  }

  // Count votes
  int votes = 0;
  for (int i = 0; i < s_vote_count; i++) {
    if (s_vote_buf[i] >= s_threshold)
      votes++;
  }

  // Runtime telemetry (helps verify model output while tuning threshold)
  static uint32_t s_frame_counter = 0;
  s_frame_counter++;
  if ((s_frame_counter % DEBUG_LOG_EVERY_FRAMES) == 0) {
    int raw_out = (s_output->type == kTfLiteUInt8)
                      ? (int)s_output->data.uint8[0]
                      : (int)s_output->data.int8[0];
    ESP_LOGI(TAG,
             "prob=%.3f raw=%d thr=%.2f votes=%d/%d cooldown=%d feat=[%u..%u] "
             "q=[%d..%d]",
             prob, raw_out, s_threshold, votes, VOTES_NEEDED, s_cooldown_left,
             (unsigned)feat_min, (unsigned)feat_max, (int)q_min, (int)q_max);
  }

  if (votes >= VOTES_NEEDED) {
    s_cooldown_left = VOTE_COOLDOWN;
    ESP_LOGI(TAG, "Wakeword 'loopy' detected! prob=%.3f votes=%d/%d", prob,
             votes, VOTES_NEEDED);
    reset_tflite_state(); // Reset streaming state after detection
    reset_voter();
    return true;
  }

  return false;
}

// ─── Public API ──────────────────────────────────────────────────────────────

extern "C" int wakeword_init(float threshold) {
  if (s_ready)
    return 0;

  s_threshold = threshold;
  ESP_LOGW(TAG, "Debug mode: threshold=%.2f, log_every=%d frames", s_threshold,
           DEBUG_LOG_EVERY_FRAMES);

  // --- TFLite Micro ---
  s_arena = (uint8_t *)heap_caps_malloc(ARENA_SIZE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_arena)
    s_arena = (uint8_t *)malloc(ARENA_SIZE);
  if (!s_arena) {
    ESP_LOGE(TAG, "Cannot allocate %d KB arena", ARENA_SIZE / 1024);
    return -1;
  }

  const tflite::Model *model = tflite::GetModel(loopy_microwakeword_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(TAG, "Model schema mismatch");
    return -1;
  }

  // Register all ops used by the model
  s_resolver.AddAssignVariable();
  s_resolver.AddCallOnce();
  s_resolver.AddConcatenation();
  s_resolver.AddConv2D();
  s_resolver.AddDepthwiseConv2D();
  s_resolver.AddFullyConnected();
  s_resolver.AddLogistic();
  s_resolver.AddQuantize();
  s_resolver.AddReadVariable();
  s_resolver.AddReshape();
  s_resolver.AddStridedSlice();
  s_resolver.AddVarHandle();

  s_allocator = tflite::MicroAllocator::Create(s_arena, ARENA_SIZE);
  if (!s_allocator) {
    ESP_LOGE(TAG, "MicroAllocator failed");
    return -1;
  }

  s_resource_vars = tflite::MicroResourceVariables::Create(s_allocator, 10);
  if (!s_resource_vars) {
    ESP_LOGE(TAG, "ResourceVariables failed");
    return -1;
  }

  s_interpreter = new tflite::MicroInterpreter(model, s_resolver, s_allocator,
                                               s_resource_vars);
  if (!s_interpreter) {
    ESP_LOGE(TAG, "MicroInterpreter failed");
    return -1;
  }

  if (s_interpreter->AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(TAG, "AllocateTensors failed - increase ARENA_SIZE (%d KB)",
             ARENA_SIZE / 1024);
    return -1;
  }

  s_input = s_interpreter->input(0);
  s_output = s_interpreter->output(0);

  size_t used = s_interpreter->arena_used_bytes();
  ESP_LOGI(TAG, "TFLite OK - arena %u/%d bytes (%.1f%%)", (unsigned)used,
           ARENA_SIZE, 100.0f * used / ARENA_SIZE);
  ESP_LOGI(TAG, "Input  shape [%d,%d,%d] type=%d scale=%.6f zp=%d",
           s_input->dims->data[0], s_input->dims->data[1],
           s_input->dims->data[2], s_input->type, s_input->params.scale,
           s_input->params.zero_point);
  ESP_LOGI(TAG, "Output shape [%d,%d] type=%d scale=%.6f zp=%d",
           s_output->dims->data[0], s_output->dims->data[1], s_output->type,
           s_output->params.scale, s_output->params.zero_point);

  run_self_test_once();

  // --- MicroFrontend ---
  FrontendConfig fe_cfg;
  FrontendFillConfigWithDefaults(&fe_cfg);
  fe_cfg.window.size_ms = 30; // 480 samples @ 16 kHz
  fe_cfg.window.step_size_ms = 10;
  fe_cfg.filterbank.num_channels = 40;
  fe_cfg.filterbank.lower_band_limit = 125.0;
  fe_cfg.filterbank.upper_band_limit = 7500.0;
  fe_cfg.pcan_gain_control.enable_pcan = 1; // Critical for MicroWakeWord models
  fe_cfg.pcan_gain_control.strength = 0.95;
  fe_cfg.pcan_gain_control.offset = 80.0;
  fe_cfg.pcan_gain_control.gain_bits = 21;

  memset(&s_fe_state, 0, sizeof(s_fe_state));
  if (!FrontendPopulateState(&fe_cfg, &s_fe_state, 16000)) {
    ESP_LOGE(TAG, "FrontendPopulateState failed");
    return -1;
  }
  s_fe_ready = true;
  ESP_LOGI(TAG, "MicroFrontend OK – window=30ms, step=10ms, bins=40, pcan=on");

  reset_tflite_state();
  reset_voter();
  s_ready = true;

  ESP_LOGI(TAG, "Wakeword detector ready, threshold=%.2f", s_threshold);
  return 0;
}

extern "C" bool wakeword_feed(const int16_t *samples, int n_samples) {
  if (!s_ready || !s_fe_ready || !samples || n_samples <= 0)
    return false;

  bool fired = false;
  int idx = 0;

  while (idx < n_samples) {
    int remaining = n_samples - idx;
    size_t consumed = 0;

    struct FrontendOutput out = FrontendProcessSamples(
        &s_fe_state, samples + idx, (size_t)remaining, &consumed);

    idx += (int)consumed;
    if (consumed == 0)
      break; // safety

    if (out.size == 0)
      continue; // still priming 30 ms window

    if (out.size != 40) {
      ESP_LOGW(TAG, "Unexpected frontend bins: %d (expected 40)",
               (int)out.size);
      continue;
    }

    // out.values is uint16_t[40]
    if (run_inference(out.values)) {
      fired = true;
    }
  }

  return fired;
}

extern "C" void wakeword_reset(void) {
  if (!s_ready)
    return;
  reset_tflite_state();
  if (s_fe_ready)
    FrontendReset(&s_fe_state);
  reset_voter();
}
