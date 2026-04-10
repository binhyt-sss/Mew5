#define LOG_TAG "SampleViVpssProbe"
#define LOG_LEVEL LOG_LEVEL_INFO

#include "middleware_utils.h"

#include "core/utils/vpss_helper.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cvi_vpss.h>
#include <cvi_sys.h>
#include <cvi_vi.h>

static volatile int g_exit = 0;

static void handle_signal(int signo) {
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  printf("\nhandle signal, signo=%d\n", signo);
  g_exit = 1;
}

static void dump_vpss_grp_attr(VPSS_GRP grp, const VPSS_GRP_ATTR_S *pstAttr, const char *label) {
  printf("\n=== VPSS_GRP_ATTR_S (%s) grp=%d ===\n", label, grp);
  if (pstAttr == NULL) {
    printf("  [NULL]\n");
    return;
  }
  printf("  u32MaxW:                     %u\n", pstAttr->u32MaxW);
  printf("  u32MaxH:                     %u\n", pstAttr->u32MaxH);
  printf("  enPixelFormat:               %d\n", pstAttr->enPixelFormat);
  printf("  stFrameRate (src/dst):       %d/%d\n", pstAttr->stFrameRate.s32SrcFrameRate, pstAttr->stFrameRate.s32DstFrameRate);
  printf("  u8VpssDev:                   %u\n", pstAttr->u8VpssDev);
  printf("\n");
}

static void dump_vpss_chn_attr(VPSS_GRP grp, VPSS_CHN chn, const VPSS_CHN_ATTR_S *pstAttr, const char *label) {
  printf("\n=== VPSS_CHN_ATTR_S (%s) grp=%d chn=%d ===\n", label, grp, chn);
  if (pstAttr == NULL) {
    printf("  [NULL]\n");
    return;
  }
  printf("  u32Width:                    %u\n", pstAttr->u32Width);
  printf("  u32Height:                   %u\n", pstAttr->u32Height);
  printf("  enVideoFormat:               %d\n", pstAttr->enVideoFormat);
  printf("  enPixelFormat:               %d\n", pstAttr->enPixelFormat);
  printf("  stFrameRate (src/dst):       %d/%d\n", pstAttr->stFrameRate.s32SrcFrameRate, pstAttr->stFrameRate.s32DstFrameRate);
  printf("  bMirror:                     %d\n", pstAttr->bMirror);
  printf("  bFlip:                       %d\n", pstAttr->bFlip);
  printf("  u32Depth:                    %u\n", pstAttr->u32Depth);
  printf("  stAspectRatio.enMode:        %d\n", pstAttr->stAspectRatio.enMode);
  printf("  stAspectRatio.bEnableBgColor:%d\n", pstAttr->stAspectRatio.bEnableBgColor);
  printf("  stAspectRatio.u32BgColor:    0x%x\n", pstAttr->stAspectRatio.u32BgColor);
  printf("  stNormalize.bEnable:         %d\n", pstAttr->stNormalize.bEnable);
  printf("\n");
}

static CVI_S32 dump_vpss_attr_from_device(VPSS_GRP grp, const char *label) {
  printf("\n>>> QUERYING VPSS ATTRIBUTES FROM DEVICE (%s) <<<\n", label);
  VPSS_GRP_ATTR_S stGrpAttr;
  CVI_S32 ret = CVI_VPSS_GetGrpAttr(grp, &stGrpAttr);
  if (ret != CVI_SUCCESS) {
    printf("CVI_VPSS_GetGrpAttr(grp=%d) failed: %#x\n", grp, ret);
    return ret;
  }
  dump_vpss_grp_attr(grp, &stGrpAttr, label);

  for (VPSS_CHN chn = 0; chn < VPSS_MAX_CHN_NUM; chn++) {
    VPSS_CHN_ATTR_S stChnAttr;
    ret = CVI_VPSS_GetChnAttr(grp, chn, &stChnAttr);
    if (ret == CVI_SUCCESS) {
      dump_vpss_chn_attr(grp, chn, &stChnAttr, label);
    }
  }

  return CVI_SUCCESS;
}

static int read_env_int(const char *name, int default_value, int min_value, int max_value);

static CVI_S32 run_vi_frame_scan(int timeout_ms, int *pViOk, int *pViFail) {
  int scan_pipes = read_env_int("SAMPLE_VI_PROBE_SCAN_PIPES", 2, 1, 8);
  int scan_chns = read_env_int("SAMPLE_VI_PROBE_SCAN_CHNS", 4, 1, 8);
  int enable_probe_chn = read_env_int("SAMPLE_VI_PROBE_ENABLE_CHN", 0, 0, 1);
  printf("VI scan range: pipes=0..%d chns=0..%d\n", scan_pipes - 1, scan_chns - 1);
  printf("VI scan behavior: enable_chn=%d\n", enable_probe_chn);

  int vi_ok = 0;
  int vi_fail = 0;
  for (int pipe = 0; pipe < scan_pipes; pipe++) {
    for (int chn = 0; chn < scan_chns; chn++) {
      if (enable_probe_chn) {
        CVI_S32 vi_enable_ret = CVI_VI_EnableChn(pipe, chn);
        printf("VI pre-test EnableChn(pipe=%d, chn=%d) ret=%#x\n", pipe, chn, vi_enable_ret);
      }

      VIDEO_FRAME_INFO_S vi_frame;
      memset(&vi_frame, 0, sizeof(vi_frame));
      CVI_S32 vi_ret = CVI_VI_GetChnFrame(pipe, chn, &vi_frame, timeout_ms);
      if (vi_ret == CVI_SUCCESS) {
        vi_ok++;
        printf("[VI probe] pipe=%d chn=%d frame=%ux%u\n", pipe, chn,
               vi_frame.stVFrame.u32Width, vi_frame.stVFrame.u32Height);
        CVI_VI_ReleaseChnFrame(pipe, chn, &vi_frame);
      } else {
        vi_fail++;
        printf("[VI probe] pipe=%d chn=%d failed: %#x\n", pipe, chn, vi_ret);
      }
    }
  }

  if (pViOk) {
    *pViOk = vi_ok;
  }
  if (pViFail) {
    *pViFail = vi_fail;
  }

  return CVI_SUCCESS;
}

static CVI_S32 run_sensor_only_probe(SAMPLE_TDL_MW_CONFIG_S *pstMWConfig, int timeout_ms,
                                     int *pViOk, int *pViFail) {
  PIC_SIZE_E enPicSize;
  CVI_S32 ret = SAMPLE_COMM_VI_GetSizeBySensor(
      pstMWConfig->stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
  if (ret != CVI_SUCCESS) {
    return ret;
  }

  SIZE_S stSensorSize;
  ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
  if (ret != CVI_SUCCESS) {
    return ret;
  }

  ret = SAMPLE_PLAT_SYS_INIT(stSensorSize);
  if (ret != CVI_SUCCESS) {
    printf("sensor-only sys init failed with %#x\n", ret);
    return ret;
  }

  ret = SAMPLE_TDL_VI_Init_Manual(&pstMWConfig->stViConfig);
  if (ret != CVI_SUCCESS) {
    printf("sensor-only VI init failed: %#x\n", ret);
    SAMPLE_COMM_SYS_Exit();
    return ret;
  }

  int skip_scan = read_env_int("SAMPLE_VI_PROBE_SKIP_SCAN", 0, 0, 1);
  if (skip_scan) {
    printf("sensor-only: skip VI frame scan by SAMPLE_VI_PROBE_SKIP_SCAN=1\n");
    if (pViOk) {
      *pViOk = 0;
    }
    if (pViFail) {
      *pViFail = 0;
    }
    ret = CVI_SUCCESS;
  } else {
    ret = run_vi_frame_scan(timeout_ms, pViOk, pViFail);
  }

  int skip_cleanup = read_env_int("SAMPLE_VI_PROBE_SKIP_CLEANUP", 0, 0, 1);
  if (skip_cleanup) {
    printf("sensor-only: skip VI/SYS cleanup by SAMPLE_VI_PROBE_SKIP_CLEANUP=1\n");
    return ret;
  }

  SAMPLE_COMM_VI_DestroyVi(&pstMWConfig->stViConfig);
  SAMPLE_COMM_SYS_Exit();
  return ret;
}

static int read_env_int(const char *name, int default_value, int min_value, int max_value) {
  const char *value = getenv(name);
  if (value == NULL || value[0] == '\0') {
    return default_value;
  }

  char *endptr = NULL;
  long parsed = strtol(value, &endptr, 10);
  if (endptr == value || *endptr != '\0') {
    printf("Invalid %s=%s, use default %d\n", name, value, default_value);
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
  const char *pszMode = getenv("SAMPLE_TDL_VPSS_MODE");
  const char *pszProfile = getenv("SAMPLE_TDL_VPSS_INPUT_PROFILE");

  pstMWConfig->stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_DUAL;
  pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
  pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;

  if (pszMode && strcmp(pszMode, "single") == 0) {
    pstMWConfig->stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_SINGLE;
  }

  if (pszProfile && strcmp(pszProfile, "dual_isp") == 0) {
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_ISP;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;
  } else if (pszProfile && strcmp(pszProfile, "dual_mem") == 0) {
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_MEM;
  }

  printf("Probe VPSS mode: %s input0=%d input1=%d\n",
         pstMWConfig->stVPSSPoolConfig.stVpssMode.enMode == VPSS_MODE_SINGLE ? "single" : "dual",
         pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0],
         pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1]);
#endif
}

static CVI_S32 build_probe_mw_config(SAMPLE_TDL_MW_CONFIG_S *pstMWConfig) {
  CVI_S32 s32Ret = SAMPLE_TDL_Get_VI_Config(&pstMWConfig->stViConfig);
  if (s32Ret != CVI_SUCCESS || pstMWConfig->stViConfig.s32WorkingViNum <= 0) {
    printf("Failed to load VI config\n");
    return CVI_FAILURE;
  }

  PIC_SIZE_E enPicSize;
  s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
      pstMWConfig->stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
  if (s32Ret != CVI_SUCCESS) {
    return s32Ret;
  }

  SIZE_S stSensorSize;
  s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
  if (s32Ret != CVI_SUCCESS) {
    return s32Ret;
  }

  /* GC2083 is always 1920x1080 — override if sensor size lookup returned wrong value */
  if (stSensorSize.u32Width < 1920) {
    stSensorSize.u32Width  = 1920;
    stSensorSize.u32Height = 1080;
    printf("Probe: overriding sensor size to 1920x1080 for GC2083\n");
  }
  SIZE_S stVencSize = {.u32Width = 1280, .u32Height = 720};

  int disable_venc = read_env_int("SAMPLE_TDL_DISABLE_VENC", 0, 0, 1);
  int vpss_chn_count = read_env_int("SAMPLE_TDL_VPSS_CHN_COUNT", 2, 1, 2);
  int bind_vi = read_env_int("SAMPLE_TDL_VPSS_BIND_VI", 1, 0, 1);

  pstMWConfig->stVBPoolConfig.u32VBPoolCount = disable_venc ? 2 : 3;

  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].enFormat = VI_PIXEL_FORMAT;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = disable_venc ? 2 : 5;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32Height = stSensorSize.u32Height;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32Width = stSensorSize.u32Width;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].bBind = CVI_TRUE;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32VpssChnBinding = VPSS_CHN0;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32VpssGrpBinding = (VPSS_GRP)0;

  pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].enFormat = VI_PIXEL_FORMAT;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32BlkCount = disable_venc ? 2 : 5;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32Height = stVencSize.u32Height;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32Width = stVencSize.u32Width;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].bBind = (vpss_chn_count > 1) ? CVI_TRUE : CVI_FALSE;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32VpssChnBinding = VPSS_CHN1;
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32VpssGrpBinding = (VPSS_GRP)0;

  if (!disable_venc) {
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].enFormat = PIXEL_FORMAT_BGR_888_PLANAR;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].u32BlkCount = 3;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].u32Height = 720;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].u32Width = 1280;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].bBind = CVI_FALSE;
  }

  pstMWConfig->stVPSSPoolConfig.u32VpssGrpCount = 1;
#ifndef CV186X
  pstMWConfig->stVPSSPoolConfig.stVpssMode.ViPipe[0] = 0;
  pstMWConfig->stVPSSPoolConfig.stVpssMode.ViPipe[1] = 0;
#endif
  configure_vpss_mode_from_env(pstMWConfig);

  int bindVB = read_env_int("SAMPLE_TDL_VPSS_ATTACH_VB", 1, 0, 1);
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].bBind = (bindVB == 1);
  pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].bBind = (bindVB == 1 && vpss_chn_count > 1);

  SAMPLE_TDL_VPSS_CONFIG_S *pstVpssConfig = &pstMWConfig->stVPSSPoolConfig.astVpssConfig[0];
  pstVpssConfig->bBindVI = bind_vi ? CVI_TRUE : CVI_FALSE;
  pstVpssConfig->u32ChnBindVI = (CVI_U32)read_env_int("SAMPLE_TDL_VI_BIND_CHN", 0, 0, 1);

  int vpssDev = read_env_int("SAMPLE_TDL_VPSS_DEV", 1, 0, 1);
  VPSS_GRP_DEFAULT_HELPER2(&pstVpssConfig->stVpssGrpAttr, stSensorSize.u32Width,
                           stSensorSize.u32Height, VI_PIXEL_FORMAT, vpssDev);
  pstVpssConfig->u32ChnCount = (CVI_U32)vpss_chn_count;
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[0], stVencSize.u32Width,
                          stVencSize.u32Height, VI_PIXEL_FORMAT, CVI_TRUE);
  if (vpss_chn_count > 1) {
    VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[1], stVencSize.u32Width,
                            stVencSize.u32Height, VI_PIXEL_FORMAT, CVI_TRUE);
  }
  pstVpssConfig->astVpssChnAttr[0].u32Depth = 4;
  if (vpss_chn_count > 1) {
    pstVpssConfig->astVpssChnAttr[1].u32Depth = 4;
  }

  printf("Probe VPSS config: chn_count=%d bind_vi=%d bind_vb=%d\n", vpss_chn_count, bind_vi,
         bindVB);

  SAMPLE_TDL_Get_Input_Config(&pstMWConfig->stVencConfig.stChnInputCfg);
  pstMWConfig->stVencConfig.u32FrameWidth = stVencSize.u32Width;
  pstMWConfig->stVencConfig.u32FrameHeight = stVencSize.u32Height;
  SAMPLE_TDL_Get_RTSP_Config(&pstMWConfig->stRTSPConfig.stRTSPConfig);

  return CVI_SUCCESS;
}

int main(int argc, char *argv[]) {
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  int force_manual = 0;
  int sensor_only_cli = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--manual") == 0) {
      force_manual = 1;
    } else if (strcmp(argv[i], "--sensor-only") == 0) {
      sensor_only_cli = 1;
    }
  }

  SAMPLE_TDL_MW_CONFIG_S stMWConfig;
  SAMPLE_TDL_MW_CONTEXT stMWContext;
  memset(&stMWConfig, 0, sizeof(stMWConfig));
  memset(&stMWContext, 0, sizeof(stMWContext));

  int fps = read_env_int("SAMPLE_VPSS_PROBE_FPS", 25, 1, 60);
  int timeout_ms = read_env_int("SAMPLE_VPSS_PROBE_TIMEOUT_MS", 1500, 100, 5000);
  int sleep_ms = read_env_int("SAMPLE_VPSS_PROBE_SLEEP_MS", 50, 0, 1000);
  int rounds = read_env_int("SAMPLE_VPSS_PROBE_ROUNDS", 120, 1, 3600);
  int enable_ch0 = read_env_int("SAMPLE_VPSS_PROBE_CH0", 1, 0, 1);
  int enable_ch1 = read_env_int("SAMPLE_VPSS_PROBE_CH1", 1, 0, 1);
  int vpss_chn_count = read_env_int("SAMPLE_TDL_VPSS_CHN_COUNT", 2, 1, 2);
  if (vpss_chn_count < 2 && enable_ch1) {
    printf("Force disable ch1 probe because SAMPLE_TDL_VPSS_CHN_COUNT=%d\n", vpss_chn_count);
    enable_ch1 = 0;
  }

  printf("Probe config: fps=%d timeout=%dms rounds=%d sleep=%dms ch0=%d ch1=%d\n", fps,
         timeout_ms, rounds, sleep_ms, enable_ch0, enable_ch1);
  printf("Probe flags: manual=%d sensor_only=%d\n", force_manual, sensor_only_cli);

  setenv("SAMPLE_TDL_DISABLE_RTSP", "1", 1);

  if (force_manual) {
    setenv("SAMPLE_TDL_FORCE_MANUAL_VI_INIT", "1", 1);
  }

  CVI_S32 ret = build_probe_mw_config(&stMWConfig);
  if (ret != CVI_SUCCESS) {
    printf("build_probe_mw_config failed: %#x\n", ret);
    return 1;
  }

  int sensor_only = sensor_only_cli || read_env_int("SAMPLE_VPSS_PROBE_SENSOR_ONLY", 0, 0, 1);
  if (sensor_only) {
    int vi_ok = 0;
    int vi_fail = 0;
    ret = run_sensor_only_probe(&stMWConfig, timeout_ms, &vi_ok, &vi_fail);
    printf("VI Frame Test Scan: ok=%d fail=%d\n", vi_ok, vi_fail);
    if (ret != CVI_SUCCESS) {
      printf("sensor-only probe failed: %#x\n", ret);
      return 1;
    }
    printf("Sensor-only mode enabled; exiting after VI scan.\n");
    return 0;
  }

  ret = SAMPLE_TDL_Init_WM(&stMWConfig, &stMWContext);
  if (ret != CVI_SUCCESS) {
    printf("SAMPLE_TDL_Init_WM failed: %#x\n", ret);
    return 1;
  }

  // Dump VPSS attributes after middleware init
  int dump_attrs = read_env_int("SAMPLE_VPSS_PROBE_DUMP_ATTRS", 0, 0, 1);
  if (dump_attrs) {
    printf("\n========== VPSS ATTRIBUTES DUMP AFTER MIDDLEWARE INIT ==========\n");
    dump_vpss_attr_from_device(0, "After_MiddlewareInit");
  }

  VPSS_GRP grp = 0;
  VPSS_CHN ch_tdl = VPSS_CHN0;
  VPSS_CHN ch_vo = VPSS_CHN1;
  printf("Probe channels: grp=%d tdl_chn=%d vo_chn=%d\n", grp, ch_tdl, ch_vo);

  int ok_tdl = 0;
  int ok_vo = 0;
  int fail_tdl = 0;
  int fail_vo = 0;

  // Test if VI has frames directly (before VPSS)
  int test_vi_frames = read_env_int("SAMPLE_VPSS_PROBE_TEST_VI_FRAMES", 1, 0, 1);
  if (test_vi_frames) {
    printf("\n*** TESTING VI FRAMES DIRECTLY ***\n");
    int scan_pipes = read_env_int("SAMPLE_VI_PROBE_SCAN_PIPES", 2, 1, 8);
    int scan_chns = read_env_int("SAMPLE_VI_PROBE_SCAN_CHNS", 4, 1, 8);
    printf("VI scan range: pipes=0..%d chns=0..%d\n", scan_pipes - 1, scan_chns - 1);
    int vi_ok = 0, vi_fail = 0;
    for (int pipe = 0; pipe < scan_pipes; pipe++) {
      for (int chn = 0; chn < scan_chns; chn++) {
        CVI_S32 vi_enable_ret = CVI_VI_EnableChn(pipe, chn);
        printf("VI pre-test EnableChn(pipe=%d, chn=%d) ret=%#x\n", pipe, chn, vi_enable_ret);

        VIDEO_FRAME_INFO_S vi_frame;
        memset(&vi_frame, 0, sizeof(vi_frame));
        CVI_S32 vi_ret = CVI_VI_GetChnFrame(pipe, chn, &vi_frame, timeout_ms);
        if (vi_ret == CVI_SUCCESS) {
          vi_ok++;
          printf("[VI probe] pipe=%d chn=%d frame=%ux%u\n", pipe, chn,
                 vi_frame.stVFrame.u32Width, vi_frame.stVFrame.u32Height);
          CVI_VI_ReleaseChnFrame(pipe, chn, &vi_frame);
        } else {
          vi_fail++;
          printf("[VI probe] pipe=%d chn=%d failed: %#x\n", pipe, chn, vi_ret);
        }
      }
    }
    printf("VI Frame Test Scan: ok=%d fail=%d\n", vi_ok, vi_fail);
    printf("*** END VI FRAME TEST ***\n\n");
  }

  for (int i = 0; i < rounds && !g_exit; i++) {
    VIDEO_FRAME_INFO_S frame;
    memset(&frame, 0, sizeof(frame));

    if (enable_ch0) {
      ret = CVI_VPSS_GetChnFrame(grp, ch_tdl, &frame, timeout_ms);
      if (ret == CVI_SUCCESS) {
        ok_tdl++;
        CVI_VPSS_ReleaseChnFrame(grp, ch_tdl, &frame);
      } else {
        fail_tdl++;
        printf("[round %d] GetChnFrame tdl_chn(%d) failed: %#x\n", i, ch_tdl, ret);
      }
    }

    memset(&frame, 0, sizeof(frame));
    if (enable_ch1) {
      ret = CVI_VPSS_GetChnFrame(grp, ch_vo, &frame, timeout_ms);
      if (ret == CVI_SUCCESS) {
        ok_vo++;
        CVI_VPSS_ReleaseChnFrame(grp, ch_vo, &frame);
      } else {
        fail_vo++;
        printf("[round %d] GetChnFrame vo_chn(%d) failed: %#x\n", i, ch_vo, ret);
      }
    }

    if ((i + 1) % 10 == 0) {
      printf("Progress %d/%d | tdl ok=%d fail=%d | vo ok=%d fail=%d\n", i + 1, rounds, ok_tdl,
             fail_tdl, ok_vo, fail_vo);
    }

    if (sleep_ms > 0) {
      usleep((unsigned int)sleep_ms * 1000);
    }
  }

  printf("Final | tdl ok=%d fail=%d | vo ok=%d fail=%d\n", ok_tdl, fail_tdl, ok_vo, fail_vo);

  SAMPLE_TDL_Destroy_MW(&stMWContext);
  return 0;
}
