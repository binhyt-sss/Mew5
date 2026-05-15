#define LOG_TAG "SampleHandGesture"
#define LOG_LEVEL LOG_LEVEL_INFO

#include "middleware_utils.h"
#include "sample_utils.h"
#include "vi_vo_utils.h"

#include <core/utils/vpss_helper.h>
#include <cvi_comm.h>
#include <rtsp.h>
#include <sample_comm.h>
#include "cvi_tdl.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "face_anim.h"
#include "ili9341.h"

static bool g_lcd_ok = false;

#define LCD_MAX_BOXES 8
#define HAND_CONF_THRESHOLD  0.65f
#define KPT_SKIP_FRAMES      3     /* run keypoint every 4th frame */
#define GESTURE_CONFIRM_N    3     /* need N consecutive same label to confirm */
#define LCD_KPT_PER_HAND 21
static int      g_lcd_gesture = -1;
static float    g_lcd_palm_dx = 0.0f;
static float    g_lcd_palm_dy = 0.0f;
static bool     g_lcd_buf_ready = false;
static pthread_mutex_t g_lcd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_lcd_cond  = PTHREAD_COND_INITIALIZER;

static volatile bool bExit = false;

MUTEXAUTOLOCK_INIT(ResultMutex);

static const char *gesture_names[] = {
    "Open Hand", "Fist",     "Point Up", "Victory",
    "ThumbsUp",  "ThumbsDown","OK",       "Rock", "Call",
};
#define GESTURE_COUNT (sizeof(gesture_names) / sizeof(gesture_names[0]))

static VPSS_CHN g_tdl_vpss_chn = VPSS_CHN1;
static VPSS_CHN g_venc_vpss_chn = VPSS_CHN0;
static int g_probe_retries = 3;
static int g_probe_timeout_ms = 500;
static int g_probe_start_delay_ms = 300;

/* Classify gesture from 21 hand keypoints.
 * Uses distance from fingertip to wrist vs MCP to wrist.
 * If tip is farther from wrist than MCP ? finger extended. */
static float kpt_dist2(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0;
  return dx*dx + dy*dy;
}
static int kpt_finger_extended(float *xn, float *yn, int tip, int mcp) {
  float d_tip = kpt_dist2(xn[tip], yn[tip], xn[0], yn[0]);
  float d_mcp = kpt_dist2(xn[mcp], yn[mcp], xn[0], yn[0]);
  return d_tip > d_mcp;
}
static int kpt_classify(cvtdl_handpose21_meta_t *kp) {
  float *xn = kp->xn, *yn = kp->yn;
  /* tip/mcp indices: index=8/5, middle=12/9, ring=16/13, pinky=20/17 */
  int index  = kpt_finger_extended(xn, yn, 8,  5);
  int middle = kpt_finger_extended(xn, yn, 12, 9);
  int ring   = kpt_finger_extended(xn, yn, 16, 13);
  int pinky  = kpt_finger_extended(xn, yn, 20, 17);
  /* Thumb: tip farther from index MCP than thumb MCP */
  int thumb  = kpt_dist2(xn[4], yn[4], xn[5], yn[5]) >
               kpt_dist2(xn[2], yn[2], xn[5], yn[5]);
  int n = index + middle + ring + pinky;
  if (n == 4)                          return 0; /* Open Hand */
  if (n == 0 && !thumb)                return 1; /* Fist */
  if (n == 1 && index)                 return 2; /* Point */
  if (n == 2 && index && middle)       return 3; /* Victory */
  if (n == 0 && thumb)                 return 4; /* Thumbs Up */
  if (n == 1 && pinky && thumb)        return 8; /* Call */
  return 1; /* default Fist */
}

void *run_lcd_thread(void *arg) {
  (void)arg;
  printf("Enter LCD thread\n");
  face_anim_t face_state;
  face_anim_init(&face_state);
  while (bExit == false) {
    int gesture = -1;
    float palm_dx = 0.0f;
    float palm_dy = 0.0f;
    pthread_mutex_lock(&g_lcd_mutex);
    while (!g_lcd_buf_ready && !bExit)
      pthread_cond_wait(&g_lcd_cond, &g_lcd_mutex);
    if (bExit) {
      pthread_mutex_unlock(&g_lcd_mutex);
      break;
    }
    gesture = g_lcd_gesture;
    palm_dx = g_lcd_palm_dx;
    palm_dy = g_lcd_palm_dy;
    g_lcd_buf_ready = false;
    pthread_mutex_unlock(&g_lcd_mutex);

    face_anim_update(&face_state, gesture, palm_dx, palm_dy);
    ili9341_render_face(&face_state);
  }
  printf("Exit LCD thread\n");
  pthread_exit(NULL);
}

static int read_env_int(const char *name, int default_value, int min_value, int max_value) {
  const char *value = getenv(name);
  if (value == NULL || value[0] == '\0') {
    return default_value;
  }

  char *endptr = NULL;
  long parsed = strtol(value, &endptr, 10);
  if (endptr == value || *endptr != '\0') {
    printf("Invalid %s=%s, keep default %d\n", name, value, default_value);
    return default_value;
  }
  if (parsed < min_value) {
    parsed = min_value;
  }
  if (parsed > max_value) {
    parsed = max_value;
  }
  return (int)parsed;
}

static void configure_vpss_mode_from_env(SAMPLE_TDL_MW_CONFIG_S *pstMWConfig) {
#ifndef CV186X
  const char *mode = getenv("SAMPLE_TDL_VPSS_MODE");
  if (mode != NULL && strcmp(mode, "single") == 0) {
    pstMWConfig->stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_SINGLE;
  } else {
    pstMWConfig->stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_DUAL;
  }

  const char *profile = getenv("SAMPLE_TDL_VPSS_INPUT_PROFILE");
  if (profile == NULL || profile[0] == '\0') {
    profile = "mem_isp";
  }

  if (strcmp(profile, "dual_isp") == 0) {
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_ISP;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;
  } else if (strcmp(profile, "dual_mem") == 0) {
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_MEM;
  } else {
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;
    profile = "mem_isp";
  }

  printf("VPSS mode/profile: %s/%s (in0=%d in1=%d)\n",
         pstMWConfig->stVPSSPoolConfig.stVpssMode.enMode == VPSS_MODE_SINGLE ? "single"
                                                                              : "dual",
         profile,
         pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0],
         pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1]);
#else
  (void)pstMWConfig;
#endif
}

static CVI_S32 probe_vpss_channel(VPSS_GRP grp, VPSS_CHN chn, int retries, int timeout_ms) {
  VIDEO_FRAME_INFO_S stFrame;
  CVI_S32 ret = CVI_FAILURE;

  for (int i = 0; i < retries; i++) {
    ret = CVI_VPSS_GetChnFrame(grp, chn, &stFrame, timeout_ms);
    if (ret == CVI_SUCCESS) {
      CVI_VPSS_ReleaseChnFrame(grp, chn, &stFrame);
      return CVI_SUCCESS;
    }
    usleep(100000);
  }

  return ret;
}

typedef struct {
  cvtdl_object_t stHandMeta;
  int gesture_label;
  float gesture_score;
  bool valid;
} GESTURE_RESULT_S;

static GESTURE_RESULT_S g_stResult = {0};

typedef struct {
  SAMPLE_TDL_MW_CONTEXT *pstMWContext;
  cvitdl_service_handle_t stServiceHandle;
} SAMPLE_TDL_VENC_THREAD_ARG_S;

void *run_venc(void *args) {
  printf("Enter encoder thread\n");
  SAMPLE_TDL_VENC_THREAD_ARG_S *pstArgs = (SAMPLE_TDL_VENC_THREAD_ARG_S *)args;
  VIDEO_FRAME_INFO_S stFrame;
  CVI_S32 s32Ret;

  /* When TDL and venc share the same channel, this thread cannot also pull
   * frames — only handle RTSP (which is already disabled) or idle. */
  if (g_venc_vpss_chn == g_tdl_vpss_chn) {
    printf("Venc thread idle: sharing CHN%d with TDL thread\n", g_venc_vpss_chn);
    while (bExit == false) {
      usleep(100000);
      MutexAutoLock(ResultMutex, lock);
      if (g_stResult.valid && g_stResult.gesture_label >= 0) {
        const char *label = (g_stResult.gesture_label < (int)GESTURE_COUNT)
                                ? gesture_names[g_stResult.gesture_label]
                                : "Unknown";
        printf("\r[Gesture] %-12s (%.1f%%)  ", label,
               g_stResult.gesture_score * 100.0f);
        fflush(stdout);
      }
    }
    printf("\nExit encoder thread\n");
    pthread_exit(NULL);
  }

  while (bExit == false) {
    s32Ret = CVI_VPSS_GetChnFrame(0, g_venc_vpss_chn, &stFrame, 2000);
    if (s32Ret != CVI_SUCCESS) {
      printf("CVI_VPSS_GetChnFrame venc chn%d failed with %#x\n", g_venc_vpss_chn,
             s32Ret);
      usleep(100000);
      continue;
    }

    {
      MutexAutoLock(ResultMutex, lock);
      if (g_stResult.valid && g_stResult.stHandMeta.size > 0) {
        CVI_TDL_Service_ObjectDrawRect(pstArgs->stServiceHandle,
                                      &g_stResult.stHandMeta, &stFrame, false,
                                      CVI_TDL_Service_GetDefaultBrush());
        const char *label = (g_stResult.gesture_label >= 0 &&
                             g_stResult.gesture_label < (int)GESTURE_COUNT)
                                ? gesture_names[g_stResult.gesture_label]
                                : "Unknown";
        printf("\r[Gesture] %-12s (%.1f%%)  ", label,
               g_stResult.gesture_score * 100.0f);
        fflush(stdout);
      }
    }

    if (pstArgs->pstMWContext->pstRtspContext != NULL) {
      s32Ret = SAMPLE_TDL_Send_Frame_RTSP(&stFrame, pstArgs->pstMWContext);
      if (s32Ret != CVI_SUCCESS) {
        printf("\nSend Output Frame NG, ret=%x\n", s32Ret);
        bExit = true;
      }
    }
    CVI_VPSS_ReleaseChnFrame(0, g_venc_vpss_chn, &stFrame);
  }

  printf("\nExit encoder thread\n");
  pthread_exit(NULL);
}

void *run_tdl_thread(void *pHandle) {
  printf("Enter TDL thread\n");
  cvitdl_handle_t pstTDLHandle = (cvitdl_handle_t)pHandle;
  VIDEO_FRAME_INFO_S stFrame;
  CVI_S32 s32Ret;

  int consec_fail = 0;
  while (bExit == false) {
    s32Ret = CVI_VPSS_GetChnFrame(0, g_tdl_vpss_chn, &stFrame, 2000);
    if (s32Ret != CVI_SUCCESS) {
      consec_fail++;
      if (consec_fail % 10 == 1)
        printf("CVI_VPSS_GetChnFrame failed %d× (last=%#x)\n", consec_fail, s32Ret);
      if (consec_fail >= 30) {
        printf("VPSS frame stall: abort after %d consecutive failures\n", consec_fail);
        bExit = true;
        break;
      }
      usleep(100000);
      continue;
    }
    consec_fail = 0;

    static int dbg_count = 0;
    if (dbg_count++ < 3)
      printf("Frame: %dx%d stride=%d fmt=%d phys=0x%llx\n",
             stFrame.stVFrame.u32Width, stFrame.stVFrame.u32Height,
             stFrame.stVFrame.u32Stride[0], stFrame.stVFrame.enPixelFormat,
             (unsigned long long)stFrame.stVFrame.u64PhyAddr[0]);

    int lcd_gesture = -1;
    float lcd_palm_dx = 0.0f;
    float lcd_palm_dy = 0.0f;
    cvtdl_object_t stHandMeta = {0};
    s32Ret = CVI_TDL_Detection(pstTDLHandle, &stFrame,
                               CVI_TDL_SUPPORTED_MODEL_HAND_DETECTION, &stHandMeta);
    if (s32Ret != CVI_TDL_SUCCESS) {
      printf("Hand detection failed!, ret=%x\n", s32Ret);
      goto inf_error;
    }

    /* Static handpose meta — avoids calloc/SDK-allocator mismatch.
     * Reused every frame, never freed between frames. */
    static cvtdl_handpose21_meta_t s_kpt_info[LCD_MAX_BOXES * 4];
    static cvtdl_handpose21_meta_ts stHandposeMeta;
    memset(&stHandposeMeta, 0, sizeof(cvtdl_handpose21_meta_ts));

    /* Persistent state across frames */
    static int   s_kpt_ctr          = 0;
    static int   s_gesture_cand     = -1;
    static int   s_gesture_cand_cnt = 0;
    static int   s_confirmed        = -1;
    static float s_palm_dx          = 0.0f;
    static float s_palm_dy          = 0.0f;

    if (stHandMeta.size == 0) {
      s_kpt_ctr = 0; s_gesture_cand = -1; s_gesture_cand_cnt = 0; s_confirmed = -1;
      MutexAutoLock(ResultMutex, lock);
      g_stResult.valid = false;
      goto send_lcd;
    }

    /* Find best-confidence hand — no model needed */
    uint32_t nHands = stHandMeta.size;
    if (nHands > LCD_MAX_BOXES) nHands = LCD_MAX_BOXES;
    float best_score = 0.0f;
    int best_idx = -1;
    for (uint32_t i = 0; i < nHands; i++) {
      float score = stHandMeta.info[i].bbox.score;
      if (score > best_score) { best_score = score; best_idx = (int)i; }
    }
    int best_label = s_confirmed;

    if (best_idx < 0 || best_score < HAND_CONF_THRESHOLD) {
      s_gesture_cand = -1; s_gesture_cand_cnt = 0;
      goto send_lcd;
    }

    /* Frame skip: run keypoint every KPT_SKIP_FRAMES+1 frames */
    if (++s_kpt_ctr <= KPT_SKIP_FRAMES) {
      lcd_gesture = s_confirmed;
      lcd_palm_dx = s_palm_dx;
      lcd_palm_dy = s_palm_dy;
      goto store_result;
    }
    s_kpt_ctr = 0;

    /* Run keypoint model on best hand */
    memset(s_kpt_info, 0, nHands * sizeof(cvtdl_handpose21_meta_t) * 4);
    stHandposeMeta.size   = nHands;
    stHandposeMeta.width  = stFrame.stVFrame.u32Width;
    stHandposeMeta.height = stFrame.stVFrame.u32Height;
    stHandposeMeta.info   = s_kpt_info;
    for (uint32_t i = 0; i < nHands; i++) {
      cvtdl_bbox_t *src = &stHandMeta.info[i].bbox;
      stHandposeMeta.info[i].bbox_x = src->x1;
      stHandposeMeta.info[i].bbox_y = src->y1;
      stHandposeMeta.info[i].bbox_w = src->x2 - src->x1;
      stHandposeMeta.info[i].bbox_h = src->y2 - src->y1;
    }
    s32Ret = CVI_TDL_HandKeypoint(pstTDLHandle, &stFrame, &stHandposeMeta);
    if (s32Ret != CVI_TDL_SUCCESS) {
      printf("Hand keypoint failed!, ret=%x\n", s32Ret);
      goto send_lcd;
    }

    if (stHandposeMeta.info != NULL) {
      cvtdl_handpose21_meta_t *kp = &stHandposeMeta.info[best_idx];
      int raw = kpt_classify(kp);

      /* Debounce: only promote to confirmed after GESTURE_CONFIRM_N same label */
      if (raw == s_gesture_cand) {
        if (++s_gesture_cand_cnt >= GESTURE_CONFIRM_N)
          s_confirmed = raw;
      } else {
        s_gesture_cand     = raw;
        s_gesture_cand_cnt = 1;
      }
      best_label = s_confirmed;

      float dx = kp->x[9] - kp->x[0];
      float dy = kp->y[9] - kp->y[0];
      float len = sqrtf(dx * dx + dy * dy);
      if (len > 1e-6f) { s_palm_dx = dx / len; s_palm_dy = dy / len; }
      lcd_gesture = best_label;
      lcd_palm_dx = s_palm_dx;
      lcd_palm_dy = s_palm_dy;
    }

  store_result:
    {
      MutexAutoLock(ResultMutex, lock);
      CVI_TDL_Free(&g_stResult.stHandMeta);
      memcpy(&g_stResult.stHandMeta, &stHandMeta, sizeof(cvtdl_object_t));
      memset(&stHandMeta, 0, sizeof(cvtdl_object_t));
      g_stResult.gesture_label = best_label;
      g_stResult.gesture_score = best_score;
      g_stResult.valid = true;
    }

  send_lcd:
    if (g_lcd_ok) {
      pthread_mutex_lock(&g_lcd_mutex);
      if (!g_lcd_buf_ready) {
        g_lcd_gesture = lcd_gesture;
        g_lcd_palm_dx = lcd_palm_dx;
        g_lcd_palm_dy = lcd_palm_dy;
        g_lcd_buf_ready = true;
        pthread_cond_signal(&g_lcd_cond);
      }
      pthread_mutex_unlock(&g_lcd_mutex);
    }

  inf_error:
    CVI_TDL_Free(&stHandMeta);
    /* stHandposeMeta.info is static — do NOT free, just clear pointer */
    stHandposeMeta.info = NULL;
    stHandposeMeta.size = 0;
    CVI_VPSS_ReleaseChnFrame(0, g_tdl_vpss_chn, &stFrame);
  }

  printf("Exit TDL thread\n");
  pthread_exit(NULL);
}

CVI_S32 get_middleware_config(SAMPLE_TDL_MW_CONFIG_S *pstMWConfig) {
  CVI_S32 s32Ret = SAMPLE_TDL_Get_VI_Config(&pstMWConfig->stViConfig);
  if (s32Ret != CVI_SUCCESS || pstMWConfig->stViConfig.s32WorkingViNum <= 0) {
    printf("Failed to get sensor info from /mnt/data/sensor_cfg.ini\n");
    return -1;
  }

  PIC_SIZE_E enPicSize;
  s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
      pstMWConfig->stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
  if (s32Ret != CVI_SUCCESS) {
    printf("Cannot get sensor size\n");
    return s32Ret;
  }

  SIZE_S stSensorSize;
  s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
  if (s32Ret != CVI_SUCCESS) {
    printf("Cannot get sensor size\n");
    return s32Ret;
  }

  /* GC2083 is always 1920x1080 — override if sensor size lookup returned wrong value */
  if (stSensorSize.u32Width < 1920) {
    stSensorSize.u32Width  = 1920;
    stSensorSize.u32Height = 1080;
    printf("Overriding sensor size to 1920x1080 for GC2083\n");
  }

  SIZE_S stVencSize = {
      .u32Width = 1280,
      .u32Height = 720,
  };

  /* Memory budget (ION ~78MB, ISP ~15MB, PLAT_SYS_INIT pool ~25MB):
   * Only allocate what's needed beyond PLAT pool:
   * Pool[0]: BGR planar for TDL inference — 2 blocks = ~5.5MB
   * VPSS uses PLAT_SYS_INIT pool (3110400 x8) directly via PLAT_VPSS_INIT.
   * Total extra: ~5.5MB. Leaves ~32MB for NPU. */
  pstMWConfig->stVBPoolConfig.u32VBPoolCount = 1;

  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].enFormat = PIXEL_FORMAT_BGR_888_PLANAR;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = 1;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32Height = stVencSize.u32Height;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32Width = stVencSize.u32Width;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].bBind = false;

  pstMWConfig->stVPSSPoolConfig.u32VpssGrpCount = 1;
#ifndef CV186X
  pstMWConfig->stVPSSPoolConfig.stVpssMode.ViPipe[0] = 0;
  pstMWConfig->stVPSSPoolConfig.stVpssMode.ViPipe[1] = 0;
#endif
  configure_vpss_mode_from_env(pstMWConfig);

  int bindVB = read_env_int("SAMPLE_TDL_VPSS_ATTACH_VB", 0, 0, 1);
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].bBind = (bindVB == 1);
  printf("VPSS attach VB pools: %s\n", bindVB == 1 ? "enabled" : "disabled");

  SAMPLE_TDL_VPSS_CONFIG_S *pstVpssConfig = &pstMWConfig->stVPSSPoolConfig.astVpssConfig[0];
  pstVpssConfig->bBindVI = true;

  int vpssDev = read_env_int("SAMPLE_TDL_VPSS_DEV", 0, 0, 1);
  printf("VPSS physical device: %d\n", vpssDev);

  VPSS_GRP_DEFAULT_HELPER2(&pstVpssConfig->stVpssGrpAttr, stSensorSize.u32Width,
                           stSensorSize.u32Height, VI_PIXEL_FORMAT, vpssDev);
  pstVpssConfig->u32ChnCount = 2;
  pstVpssConfig->u32ChnBindVI = (VPSS_CHN)read_env_int("SAMPLE_TDL_VI_BIND_CHN", 0, 0, 1);
  printf("VI bind channel for VPSS: %u\n", pstVpssConfig->u32ChnBindVI);
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[0], stVencSize.u32Width,
                          stVencSize.u32Height, VI_PIXEL_FORMAT, true);
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[1], stVencSize.u32Width,
                          stVencSize.u32Height, VI_PIXEL_FORMAT, true);
  pstVpssConfig->astVpssChnAttr[0].u32Depth = 2;
  pstVpssConfig->astVpssChnAttr[1].u32Depth = 2;

  SAMPLE_TDL_Get_Input_Config(&pstMWConfig->stVencConfig.stChnInputCfg);
  pstMWConfig->stVencConfig.u32FrameWidth = stVencSize.u32Width;
  pstMWConfig->stVencConfig.u32FrameHeight = stVencSize.u32Height;

  SAMPLE_TDL_Get_RTSP_Config(&pstMWConfig->stRTSPConfig.stRTSPConfig);

  return CVI_SUCCESS;
}

static void SampleHandleSig(CVI_S32 signo) {
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  printf("\nhandle signal, signo: %d\n", signo);
  if (SIGINT == signo || SIGTERM == signo) {
    bExit = true;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    printf(
        "\nUsage: %s <hand_det_model> <hand_kpt_model> <hand_kpt_cls_model>\n\n"
        "Example:\n"
        "  %s /mnt/cvimodel/yolov8n_det_hand_384_640_INT8_cv181x.cvimodel"
        " /mnt/cvimodel/keypoint_hand_128_128_INT8_cv181x.cvimodel"
        " /mnt/cvimodel/cls_keypoint_hand_gesture_1_42_INT8_cv181x.cvimodel\n",
        argv[0], argv[0]);
    return CVI_TDL_FAILURE;
  }
  signal(SIGINT, SampleHandleSig);
  signal(SIGTERM, SampleHandleSig);

  /* This app uses LCD display only, not RTSP streaming.
   * Disabling VENC avoids allocating VENC_0_BitStreamBuffer (4MB ION)
   * which is never freed by the kernel driver on exit. */
  setenv("SAMPLE_TDL_DISABLE_VENC", "1", 0);

  SAMPLE_TDL_MW_CONFIG_S stMWConfig = {0};
  CVI_S32 s32Ret = get_middleware_config(&stMWConfig);
  if (s32Ret != CVI_SUCCESS) {
    printf("get middleware configuration failed! ret=%x\n", s32Ret);
    return -1;
  }

  SAMPLE_TDL_MW_CONTEXT stMWContext = {0};
  s32Ret = SAMPLE_TDL_Init_WM(&stMWConfig, &stMWContext);
  if (s32Ret != CVI_SUCCESS) {
    printf("init middleware failed! ret=%x\n", s32Ret);
    return -1;
  }

  if (ili9341_open() == 0) {
    g_lcd_ok = true;
    ili9341_fill(0x0000);  /* clear to black */
    printf("ST7789 LCD ready\n");
  } else {
    printf("ST7789 LCD init failed (no LCD attached?), continuing without display\n");
  }

  if (getenv("SAMPLE_TDL_DISABLE_RTSP") != NULL && strcmp(getenv("SAMPLE_TDL_DISABLE_RTSP"), "1") == 0) {
    printf("SAMPLE_TDL_DISABLE_RTSP=1, run without RTSP output\n");
  }

    g_probe_start_delay_ms = read_env_int("SAMPLE_TDL_VPSS_PROBE_DELAY_MS", 300, 0, 5000);
    g_probe_timeout_ms = read_env_int("SAMPLE_TDL_VPSS_PROBE_TIMEOUT_MS", 500, 100, 5000);
    g_probe_retries = read_env_int("SAMPLE_TDL_VPSS_PROBE_RETRIES", 3, 1, 20);
    printf("VPSS probe config: delay=%dms timeout=%dms retries=%d\n",
      g_probe_start_delay_ms, g_probe_timeout_ms, g_probe_retries);

    usleep((useconds_t)g_probe_start_delay_ms * 1000);
    CVI_S32 probeCh0 = probe_vpss_channel(0, VPSS_CHN0, g_probe_retries, g_probe_timeout_ms);
    CVI_S32 probeCh1 = probe_vpss_channel(0, VPSS_CHN1, g_probe_retries, g_probe_timeout_ms);
  printf("VPSS probe: ch0=%#x ch1=%#x\n", probeCh0, probeCh1);

  g_venc_vpss_chn = VPSS_CHN0;
  if (probeCh1 == CVI_SUCCESS) {
    g_tdl_vpss_chn = VPSS_CHN1;
  } else if (probeCh0 == CVI_SUCCESS) {
    g_tdl_vpss_chn = VPSS_CHN0;
    printf("Warning: VPSS CHN1 unavailable, fallback TDL to CHN0\n");
  } else {
    printf("Error: both VPSS channels failed probe (ch0=%#x ch1=%#x), abort.\n",
           probeCh0, probeCh1);
    s32Ret = -1;
    goto create_tdl_fail;
  }

  printf("VPSS mapping: venc->chn%d, tdl->chn%d\n", g_venc_vpss_chn,
         g_tdl_vpss_chn);

  cvitdl_handle_t stTDLHandle = NULL;
  GOTO_IF_FAILED(CVI_TDL_CreateHandle2(&stTDLHandle, 1, 0), s32Ret, create_tdl_fail);
  GOTO_IF_FAILED(CVI_TDL_SetVBPool(stTDLHandle, 0, 0), s32Ret, create_service_fail);
  CVI_TDL_SetVpssTimeout(stTDLHandle, 1000);

  cvitdl_service_handle_t stServiceHandle = NULL;
  GOTO_IF_FAILED(CVI_TDL_Service_CreateHandle(&stServiceHandle, stTDLHandle), s32Ret,
                 create_service_fail);

  GOTO_IF_FAILED(CVI_TDL_OpenModel(stTDLHandle,
                 CVI_TDL_SUPPORTED_MODEL_HAND_DETECTION, argv[1]),
                 s32Ret, setup_tdl_fail);
  GOTO_IF_FAILED(CVI_TDL_OpenModel(stTDLHandle,
                 CVI_TDL_SUPPORTED_MODEL_HAND_KEYPOINT, argv[2]),
                 s32Ret, setup_tdl_fail);
  (void)argv[3]; /* cls model: SDK HandKeypointClassification crashes internally */

  printf("Models loaded. RTSP: rtsp://192.168.42.1/h264\n");

  pthread_t stVencThread, stTDLThread, stLCDThread;
  SAMPLE_TDL_VENC_THREAD_ARG_S args = {
      .pstMWContext = &stMWContext,
      .stServiceHandle = stServiceHandle,
  };

  if (g_lcd_ok)
    pthread_create(&stLCDThread, NULL, run_lcd_thread, NULL);
  pthread_create(&stVencThread, NULL, run_venc, &args);
  pthread_create(&stTDLThread, NULL, run_tdl_thread, stTDLHandle);

  pthread_join(stTDLThread, NULL);
  pthread_join(stVencThread, NULL);
  if (g_lcd_ok) {
    pthread_mutex_lock(&g_lcd_mutex);
    pthread_cond_signal(&g_lcd_cond);  /* wake LCD thread so it can exit */
    pthread_mutex_unlock(&g_lcd_mutex);
    pthread_join(stLCDThread, NULL);
  }

setup_tdl_fail:
  CVI_TDL_Service_DestroyHandle(stServiceHandle);
create_service_fail:
  CVI_TDL_DestroyHandle(stTDLHandle);
create_tdl_fail:
  if (g_lcd_ok) {
    ili9341_fill(0x0000);
    ili9341_close();
  }
  SAMPLE_TDL_Destroy_MW(&stMWContext);

  return 0;
}
