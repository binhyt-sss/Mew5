#define LOG_TAG "MiddlewareUtils"
#define LOG_LEVEL LOG_LEVEL_INFO
#include "middleware_utils.h"

#include <cvi_isp.h>
#include <cvi_vi.h>
#include <cvi_venc.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------
 * Minimal sensor object type — mirrors ISP_SNS_OBJ_S from the SDK.
 * We only need a subset of function pointers; the rest are void* so
 * the struct layout stays binary-compatible with libsns_gc2083.so.
 * Derived from nihui/opencv-mobile capture_cvi.cpp (Apache-2.0).
 * ------------------------------------------------------------------ */
/* ISP_SNS_COMMBUS_U is a union — on RISC-V ABI a 1-byte union is passed
 * as a register (CVI_S8 sign-extended to 64-bit). Use CVI_S32 to match
 * the calling convention used by libsns_gc2083.so. */
typedef struct {
  /* pfnRegisterCallback(VI_PIPE, ALG_LIB_S*, ALG_LIB_S*) — inits sensor state */
  CVI_S32 (*pfnRegisterCallback)(CVI_S32 ViPipe, void *pstAeLib, void *pstAwbLib);
  void *pfnUnRegisterCallback;
  /* pfnSetBusInfo(VI_PIPE, ISP_SNS_COMMBUS_U) */
  CVI_S32 (*pfnSetBusInfo)(CVI_S32 ViPipe, CVI_S32 unSNSBusInfo);
  void *pfnStandby;
  void *pfnRestart;
  void *pfnMirrorFlip;
  void *pfnWriteReg;
  void *pfnReadReg;
  /* pfnSetInit(VI_PIPE, ISP_INIT_ATTR_S*) */
  CVI_S32 (*pfnSetInit)(CVI_S32 ViPipe, void *pstInitAttr);
  void *pfnPatchRxAttr;
  /* pfnPatchI2cAddr(CVI_S32 addr) */
  void (*pfnPatchI2cAddr)(CVI_S32 s32I2cAddr);
  /* pfnGetRxAttr(VI_PIPE, SNS_COMBO_DEV_ATTR_S*) — opaque 512-byte buf */
  CVI_S32 (*pfnGetRxAttr)(CVI_S32 ViPipe, void *pstDevAttr);
  void *pfnExpSensorCb;
  void *pfnExpAeCb;
  /* pfnSnsProbe(VI_PIPE) */
  CVI_S32 (*pfnSnsProbe)(CVI_S32 ViPipe);
} SNS_OBJ_MINI_S;

static CVI_S32 g_s32Gc2083I2cFd = -1;

CVI_S32 SAMPLE_TDL_VI_Init_Manual(SAMPLE_VI_CONFIG_S *pstViConfig) {
  /* ----------------------------------------------------------------
   * Step 1: Load libsns_gc2083.so and get sensor object.
   * Reference: nihui/opencv-mobile, confirmed working on Duo256M+GC2083.
   * SAMPLE_COMM_VI_StartMIPI wraps these calls but silently skips them
   * when sensor bus/addr are not pre-configured on this firmware version.
   * ---------------------------------------------------------------- */
  void *libsns = dlopen("libsns_gc2083.so", RTLD_LAZY | RTLD_GLOBAL);
  if (!libsns) {
    printf("manual VI init: dlopen libsns_gc2083.so failed: %s\n", dlerror());
    return CVI_FAILURE;
  }

  SNS_OBJ_MINI_S *pstSnsObj = (SNS_OBJ_MINI_S *)dlsym(libsns, "stSnsGc2083_Obj");
  if (!pstSnsObj) {
    printf("manual VI init: dlsym stSnsGc2083_Obj failed: %s\n", dlerror());
    dlclose(libsns);
    return CVI_FAILURE;
  }
  printf("manual VI init: stSnsGc2083_Obj @ %p\n", (void *)pstSnsObj);

  VI_PIPE ViPipe = 0;

  /* ----------------------------------------------------------------
   * Step 2: pfnPatchRxAttr (optional — configure RX lane overrides)
   * Step 3: pfnSetInit — allocates/initialises sensor state context.
   *         Must be called BEFORE pfnGetRxAttr or pfnSnsProbe.
   * Step 4: pfnSetBusInfo + pfnPatchI2cAddr — set I2C bus & address.
   * Step 5: pfnRegisterCallback — registers sensor with ISP; this
   *         also initialises the internal sensor state (g_pastGc2083).
   *         Without it, pfnGetRxAttr returns 0xc00e8006 (NULL ctx).
   * Reference: nihui/opencv-mobile capture_cvi.cpp open() sequence.
   * ---------------------------------------------------------------- */

  /* pfnSetBusInfo — ISP_SNS_COMMBUS_U (1-byte union), pass as CVI_S32 */
  if (pstSnsObj->pfnSetBusInfo) {
    CVI_S32 s32Ret = pstSnsObj->pfnSetBusInfo(ViPipe, 2);
    printf("manual VI init: pfnSetBusInfo(bus=2) ret=0x%x\n", s32Ret);
  }

  /* pfnPatchI2cAddr — GC2083 wire address 0x37 */
  if (pstSnsObj->pfnPatchI2cAddr) {
    pstSnsObj->pfnPatchI2cAddr(0x37);
    printf("manual VI init: pfnPatchI2cAddr(0x37)\n");
  }

  /* ----------------------------------------------------------------
   * Step 6: Start sensor and VI Dev (needed before MIPI)
   * ---------------------------------------------------------------- */
  CVI_S32 s32StartSensorRet = SAMPLE_COMM_VI_StartSensor(pstViConfig);
  printf("manual VI init: StartSensor ret=0x%x\n", s32StartSensorRet);
  if (s32StartSensorRet != CVI_SUCCESS) {
    return s32StartSensorRet;
  }

  for (CVI_S32 i = 0; i < pstViConfig->s32WorkingViNum; i++) {
    CVI_S32 s32Ret = SAMPLE_COMM_VI_StartDev(&pstViConfig->astViInfo[i]);
    if (s32Ret != CVI_SUCCESS) {
      printf("manual VI init: StartDev[%d] failed 0x%x\n", i, s32Ret);
      return s32Ret;
    }
  }

  CVI_S32 s32SdkStartMipiRet = SAMPLE_COMM_VI_StartMIPI(pstViConfig);
  printf("manual VI init: StartMIPI ret=0x%x\n", s32SdkStartMipiRet);

  /* ----------------------------------------------------------------
   * Step 7: MIPI init sequence — direct low-level calls.
   * Some firmware builds return StartMIPI success without touching MIPI/I2C,
   * so apply RX attrs and run SensorProbe explicitly.
   * ---------------------------------------------------------------- */
  if (s32SdkStartMipiRet != CVI_SUCCESS) {
    printf("manual VI init: StartMIPI failed, continue with explicit MIPI path\n");
  }

  unsigned char stDevAttr[512];
  memset(stDevAttr, 0, sizeof(stDevAttr));
  if (pstSnsObj->pfnGetRxAttr) {
    CVI_S32 s32Ret = pstSnsObj->pfnGetRxAttr(ViPipe, stDevAttr);
    printf("manual VI init: pfnGetRxAttr ret=0x%x\n", s32Ret);
  }

  printf("manual VI init: setting I2C pinmux for GC2083\n");
  CVI_S32 s32PinmuxRet1 = system("cvi-pinmux -w PAD_MIPIRX1P/IIC1_SDA || cvi_pinmux -w PAD_MIPIRX1P/IIC1_SDA");
  CVI_S32 s32PinmuxRet2 = system("cvi-pinmux -w PAD_MIPIRX0N/IIC1_SCL || cvi_pinmux -w PAD_MIPIRX0N/IIC1_SCL");
  if (s32PinmuxRet1 != 0 || s32PinmuxRet2 != 0) {
    printf("manual VI init: pinmux command failed ret1=%d ret2=%d\n", s32PinmuxRet1,
           s32PinmuxRet2);
  }

  if (g_s32Gc2083I2cFd < 0) {
    g_s32Gc2083I2cFd = SAMPLE_COMM_I2C_Open("/dev/i2c-2");
    printf("manual VI init: SAMPLE_COMM_I2C_Open(/dev/i2c-2) ret=%d\n", g_s32Gc2083I2cFd);
  }

  CVI_MIPI_SetSensorReset(0, 1);
  CVI_MIPI_SetMipiReset(0, 1);
  CVI_S32 s32MipiRet = CVI_MIPI_SetMipiAttr(0, stDevAttr);
  printf("manual VI init: CVI_MIPI_SetMipiAttr ret=0x%x\n", s32MipiRet);
  CVI_MIPI_SetSensorClock(0, 1);
  usleep(20000);
  CVI_MIPI_SetSensorReset(0, 0);
  usleep(100000);

  CVI_S32 s32SensorProbeRet = SAMPLE_COMM_VI_SensorProbe(pstViConfig);
  printf("manual VI init: SAMPLE_COMM_VI_SensorProbe ret=0x%x\n", s32SensorProbeRet);
  if (s32SensorProbeRet != CVI_SUCCESS && s32SdkStartMipiRet != CVI_SUCCESS) {
    return s32SensorProbeRet;
  }

  /* ----------------------------------------------------------------
   * Step 5: Create + start VI pipe
   * ---------------------------------------------------------------- */
  PIC_SIZE_E enPicSize;
  SIZE_S stSensorSize;
  CVI_S32 s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
      pstViConfig->astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
  if (s32Ret != CVI_SUCCESS) {
    printf("manual VI init: GetSizeBySensor failed 0x%x\n", s32Ret);
    return s32Ret;
  }
  s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
  if (s32Ret != CVI_SUCCESS) {
    printf("manual VI init: GetPicSize failed 0x%x\n", s32Ret);
    return s32Ret;
  }

  const char *pszPipeW = getenv("SAMPLE_TDL_MANUAL_PIPE_WIDTH");
  const char *pszPipeH = getenv("SAMPLE_TDL_MANUAL_PIPE_HEIGHT");
  if (pszPipeW && pszPipeH) {
    int reqW = atoi(pszPipeW);
    int reqH = atoi(pszPipeH);
    if (reqW > 0 && reqH > 0 && (CVI_U32)reqW <= stSensorSize.u32Width &&
        (CVI_U32)reqH <= stSensorSize.u32Height) {
      printf("manual VI init: override pipe size to %dx%d\n", reqW, reqH);
      stSensorSize.u32Width = (CVI_U32)reqW;
      stSensorSize.u32Height = (CVI_U32)reqH;
    }
  }

  VI_PIPE_ATTR_S stPipeAttr;
  memset(&stPipeAttr, 0, sizeof(stPipeAttr));
  stPipeAttr.bYuvSkip = CVI_FALSE;
  stPipeAttr.u32MaxW = stSensorSize.u32Width;
  stPipeAttr.u32MaxH = stSensorSize.u32Height;
  stPipeAttr.enPixFmt = PIXEL_FORMAT_RGB_BAYER_10BPP; /* GC2083 is 10-bit */
  stPipeAttr.enBitWidth = DATA_BITWIDTH_10;
  stPipeAttr.stFrameRate.s32SrcFrameRate = -1;
  stPipeAttr.stFrameRate.s32DstFrameRate = -1;
  stPipeAttr.bNrEn = CVI_TRUE;
  stPipeAttr.bYuvBypassPath = CVI_FALSE;
  stPipeAttr.enCompressMode = COMPRESS_MODE_NONE;

  s32Ret = CVI_VI_CreatePipe(ViPipe, &stPipeAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("manual VI init: CreatePipe failed 0x%x\n", s32Ret);
    return s32Ret;
  }
  s32Ret = CVI_VI_StartPipe(ViPipe);
  if (s32Ret != CVI_SUCCESS) {
    printf("manual VI init: StartPipe failed 0x%x\n", s32Ret);
    return s32Ret;
  }

  /* ----------------------------------------------------------------
   * Step 6: ISP + VI channels
   * ---------------------------------------------------------------- */
  s32Ret = SAMPLE_COMM_VI_CreateIsp(pstViConfig);
  if (s32Ret != CVI_SUCCESS) {
    printf("manual VI init: CreateIsp failed 0x%x\n", s32Ret);
    return s32Ret;
  }

  s32Ret = SAMPLE_COMM_VI_StartViChn(pstViConfig);
  if (s32Ret != CVI_SUCCESS) {
    printf("manual VI init: StartViChn failed 0x%x\n", s32Ret);
    return s32Ret;
  }

  for (CVI_S32 i = 0; i < pstViConfig->s32WorkingViNum; i++) {
    VI_PIPE viPipe = pstViConfig->astViInfo[i].stPipeInfo.aPipe[0];
    VI_CHN viChn = pstViConfig->astViInfo[i].stChnInfo.ViChn;
    if (viPipe < 0 || viChn < 0) {
      continue;
    }

    VI_CHN_ATTR_S stChnAttr;
    memset(&stChnAttr, 0, sizeof(stChnAttr));
    s32Ret = CVI_VI_GetChnAttr(viPipe, viChn, &stChnAttr);
    if (s32Ret == CVI_SUCCESS) {
      stChnAttr.u32Depth = 2;
      CVI_S32 s32SetRet = CVI_VI_SetChnAttr(viPipe, viChn, &stChnAttr);
      printf("manual VI init: SetChnAttr(pipe=%d, chn=%d, depth=2) ret=0x%x\n",
             viPipe, viChn, s32SetRet);
    } else {
      printf("manual VI init: GetChnAttr(pipe=%d, chn=%d) failed 0x%x\n", viPipe, viChn,
             s32Ret);
    }
  }

  return CVI_SUCCESS;
}

static CVI_S32 SAMPLE_TDL_VI_Init_FactoryLike(SAMPLE_VI_CONFIG_S *pstViConfig) {
  CVI_S32 s32Ret = SAMPLE_COMM_VI_StartSensor(pstViConfig);
  if (s32Ret != CVI_SUCCESS) {
    printf("factory-like VI init: StartSensor failed 0x%x\n", s32Ret);
    return s32Ret;
  }

  for (CVI_S32 i = 0; i < pstViConfig->s32WorkingViNum; i++) {
    s32Ret = SAMPLE_COMM_VI_StartDev(&pstViConfig->astViInfo[i]);
    if (s32Ret != CVI_SUCCESS) {
      printf("factory-like VI init: StartDev[%d] failed 0x%x\n", i, s32Ret);
      return s32Ret;
    }
  }

  s32Ret = SAMPLE_COMM_VI_StartMIPI(pstViConfig);
  if (s32Ret != CVI_SUCCESS) {
    printf("factory-like VI init: StartMIPI failed 0x%x\n", s32Ret);
    return s32Ret;
  }

  for (CVI_S32 i = 0; i < pstViConfig->s32WorkingViNum; i++) {
    VI_PIPE ViPipe = pstViConfig->astViInfo[i].stPipeInfo.aPipe[0];
    if (ViPipe < 0) {
      continue;
    }

    PIC_SIZE_E enPicSize;
    SIZE_S stSensorSize;
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(pstViConfig->astViInfo[i].stSnsInfo.enSnsType, &enPicSize);
    if (s32Ret != CVI_SUCCESS) {
      printf("factory-like VI init: GetSizeBySensor[%d] failed 0x%x\n", i, s32Ret);
      return s32Ret;
    }

    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
    if (s32Ret != CVI_SUCCESS) {
      printf("factory-like VI init: GetPicSize[%d] failed 0x%x\n", i, s32Ret);
      return s32Ret;
    }

    VI_PIPE_ATTR_S stPipeAttr;
    memset(&stPipeAttr, 0, sizeof(stPipeAttr));
    stPipeAttr.bYuvSkip = CVI_FALSE;
    stPipeAttr.u32MaxW = stSensorSize.u32Width;
    stPipeAttr.u32MaxH = stSensorSize.u32Height;
    stPipeAttr.enPixFmt = PIXEL_FORMAT_RGB_BAYER_10BPP;
    stPipeAttr.enBitWidth = DATA_BITWIDTH_10;
    stPipeAttr.stFrameRate.s32SrcFrameRate = -1;
    stPipeAttr.stFrameRate.s32DstFrameRate = -1;
    stPipeAttr.bNrEn = CVI_TRUE;
    stPipeAttr.bYuvBypassPath = CVI_FALSE;
    stPipeAttr.enCompressMode = COMPRESS_MODE_NONE;

    s32Ret = CVI_VI_CreatePipe(ViPipe, &stPipeAttr);
    if (s32Ret != CVI_SUCCESS) {
      printf("factory-like VI init: CreatePipe(pipe=%d) failed 0x%x\n", ViPipe, s32Ret);
      return s32Ret;
    }

    s32Ret = CVI_VI_StartPipe(ViPipe);
    if (s32Ret != CVI_SUCCESS) {
      printf("factory-like VI init: StartPipe(pipe=%d) failed 0x%x\n", ViPipe, s32Ret);
      return s32Ret;
    }
  }

  s32Ret = SAMPLE_COMM_VI_CreateIsp(pstViConfig);
  if (s32Ret != CVI_SUCCESS) {
    printf("factory-like VI init: CreateIsp failed 0x%x\n", s32Ret);
    return s32Ret;
  }

  s32Ret = SAMPLE_COMM_VI_StartViChn(pstViConfig);
  if (s32Ret != CVI_SUCCESS) {
    printf("factory-like VI init: StartViChn failed 0x%x\n", s32Ret);
    return s32Ret;
  }

  return CVI_SUCCESS;
}

static void SAMPLE_TDL_RTSP_ON_CONNECT(const char *ip, void *arg) {
  printf("RTSP client connected from: %s\n", ip);
}

static void SAMPLE_TDL_RTSP_ON_DISCONNECT(const char *ip, void *arg) {
  printf("RTSP client disconnected from: %s\n", ip);
}

CVI_S32 SAMPLE_TDL_Get_VI_Config(SAMPLE_VI_CONFIG_S *pstViConfig) {
  // Default sensor config parameters

  SAMPLE_INI_CFG_S stIniCfg = {
      .enSource = VI_PIPE_FRAME_SOURCE_DEV,
      .devNum = 1,
      .enSnsType[0] = SONY_IMX327_MIPI_2M_30FPS_12BIT,
      .enWDRMode[0] = WDR_MODE_NONE,
      .s32BusId[0] = 3,
      .s32SnsI2cAddr[0] = -1,
      .MipiDev[0] = 0xFF,
      .u8UseMultiSns = 0,
  };

  memset(pstViConfig, 0, sizeof(*pstViConfig));

  // Set ini path explicitly — required for IniToViCfg to work correctly.
  SAMPLE_COMM_VI_SetIniPath("/mnt/data/sensor_cfg.ini");

  // Get config from ini if found.
  if (SAMPLE_COMM_VI_ParseIni(&stIniCfg)) {
    printf("sensor info is loaded from ini file.\n");
  }

  // convert ini config to vi config.
  // NOTE: GetSensorInfo must be called AFTER IniToViCfg sets sensor type,
  // otherwise it has no sensor type to look up and returns an empty config.
  CVI_S32 s32CfgRet = SAMPLE_COMM_VI_IniToViCfg(&stIniCfg, pstViConfig);
  if (s32CfgRet != CVI_SUCCESS || pstViConfig->s32WorkingViNum <= 0) {
    printf("cannot conver ini to vi config, try GC2083 fallback. ret=0x%x workingViNum=%d\n",
           s32CfgRet, pstViConfig->s32WorkingViNum);

    // Fallback for Milk-V Duo 256M GC2083 profile.
    stIniCfg.enSource = VI_PIPE_FRAME_SOURCE_DEV;
    stIniCfg.devNum = 1;
    stIniCfg.u8UseMultiSns = 0;
    stIniCfg.enSnsType[0] = GCORE_GC2083_MIPI_2M_30FPS_10BIT;
    stIniCfg.enWDRMode[0] = WDR_MODE_NONE;
    stIniCfg.s32BusId[0] = 2;
    stIniCfg.s32SnsI2cAddr[0] = 55;
    stIniCfg.MipiDev[0] = 0;
    stIniCfg.as16LaneId[0][0] = 1;
    stIniCfg.as16LaneId[0][1] = 0;
    stIniCfg.as16LaneId[0][2] = 2;
    stIniCfg.as16LaneId[0][3] = -1;
    stIniCfg.as16LaneId[0][4] = -1;
    stIniCfg.as8PNSwap[0][0] = 0;
    stIniCfg.as8PNSwap[0][1] = 0;
    stIniCfg.as8PNSwap[0][2] = 0;
    stIniCfg.as8PNSwap[0][3] = 0;
    stIniCfg.as8PNSwap[0][4] = 0;

    memset(pstViConfig, 0, sizeof(*pstViConfig));
    SAMPLE_COMM_VI_GetSensorInfo(pstViConfig);
    s32CfgRet = SAMPLE_COMM_VI_IniToViCfg(&stIniCfg, pstViConfig);
    if (s32CfgRet != CVI_SUCCESS || pstViConfig->s32WorkingViNum <= 0) {
      printf("GC2083 fallback ini-to-vi failed. ret=0x%x workingViNum=%d\n", s32CfgRet,
             pstViConfig->s32WorkingViNum);

      // Last-resort fallback: synthesize a minimal single-sensor VI config for GC2083.
      SAMPLE_VI_INFO_S *pstViInfo = &pstViConfig->astViInfo[0];
      memset(pstViConfig, 0, sizeof(*pstViConfig));

      pstViInfo->stSnsInfo.enSnsType = GCORE_GC2083_MIPI_2M_30FPS_10BIT;
      pstViInfo->stSnsInfo.s32SnsId = 0;
      pstViInfo->stSnsInfo.s32BusId = 2;
      pstViInfo->stSnsInfo.s32SnsI2cAddr = 55;
      pstViInfo->stSnsInfo.MipiDev = 0;
      pstViInfo->stSnsInfo.as16LaneId[0] = 1;
      pstViInfo->stSnsInfo.as16LaneId[1] = 0;
      pstViInfo->stSnsInfo.as16LaneId[2] = 2;
      pstViInfo->stSnsInfo.as16LaneId[3] = -1;
      pstViInfo->stSnsInfo.as16LaneId[4] = -1;
      pstViInfo->stSnsInfo.as8PNSwap[0] = 0;
      pstViInfo->stSnsInfo.as8PNSwap[1] = 0;
      pstViInfo->stSnsInfo.as8PNSwap[2] = 0;
      pstViInfo->stSnsInfo.as8PNSwap[3] = 0;
      pstViInfo->stSnsInfo.as8PNSwap[4] = 0;

      pstViInfo->stDevInfo.ViDev = 0;
      pstViInfo->stDevInfo.enWDRMode = WDR_MODE_NONE;

      pstViInfo->stPipeInfo.aPipe[0] = 0;
      pstViInfo->stPipeInfo.aPipe[1] = -1;
      pstViInfo->stPipeInfo.aPipe[2] = -1;
      pstViInfo->stPipeInfo.aPipe[3] = -1;
      pstViInfo->stPipeInfo.enMastPipeMode = VI_OFFLINE_VPSS_ONLINE;

      pstViInfo->stChnInfo.ViChn = 0;
      pstViInfo->stChnInfo.enPixFormat = VI_PIXEL_FORMAT;
      pstViInfo->stChnInfo.enDynamicRange = DYNAMIC_RANGE_SDR8;
      pstViInfo->stChnInfo.enVideoFormat = VIDEO_FORMAT_LINEAR;
      pstViInfo->stChnInfo.enCompressMode = COMPRESS_MODE_NONE;

      pstViConfig->as32WorkingViId[0] = 0;
      pstViConfig->s32WorkingViNum = 1;
      pstViConfig->bViRotation = CVI_FALSE;

      printf("Using synthesized GC2083 VI config fallback.\n");
      return CVI_SUCCESS;
    }

    printf("GC2083 fallback ini-to-vi success.\n");
    return CVI_SUCCESS;
  }

  return CVI_SUCCESS;
}

void SAMPLE_TDL_Get_Input_Config(SAMPLE_COMM_CHN_INPUT_CONFIG_S *pstInCfg) {
  strcpy(pstInCfg->codec, "h264");
  pstInCfg->initialDelay = CVI_INITIAL_DELAY_DEFAULT;
  pstInCfg->width = 3840;
  pstInCfg->height = 2160;
  pstInCfg->vpssGrp = 1;
  pstInCfg->vpssChn = 0;
  pstInCfg->num_frames = -1;
  pstInCfg->bsMode = 0;
  pstInCfg->rcMode = SAMPLE_RC_CBR;
  pstInCfg->iqp = DEF_IQP;
  pstInCfg->pqp = DEF_PQP;
  pstInCfg->gop = DEF_264_GOP;
  pstInCfg->maxIprop = CVI_H26X_MAX_I_PROP_DEFAULT;
  pstInCfg->minIprop = CVI_H26X_MIN_I_PROP_DEFAULT;
  pstInCfg->bitrate = 8000;
  pstInCfg->firstFrmstartQp = 30;
  pstInCfg->minIqp = DEF_264_MINIQP;
  pstInCfg->maxIqp = DEF_264_MAXIQP;
  pstInCfg->minQp = DEF_264_MINQP;
  pstInCfg->maxQp = DEF_264_MAXQP;
  pstInCfg->srcFramerate = 30;
  pstInCfg->framerate = 30;
  pstInCfg->bVariFpsEn = 0;
  pstInCfg->maxbitrate = -1;
  pstInCfg->statTime = -1;
  pstInCfg->chgNum = -1;
  pstInCfg->quality = -1;
  pstInCfg->pixel_format = 0;
  pstInCfg->bitstreamBufSize = 0;
  pstInCfg->single_LumaBuf = 0;
  pstInCfg->single_core = 0;
  pstInCfg->forceIdr = -1;
  pstInCfg->tempLayer = 0;
  pstInCfg->testRoi = 0;
  pstInCfg->bgInterval = 0;
#ifdef CV186X
  pstInCfg->u32GopPreset = GOP_PRESET_IDX_IPPPP;
#endif
}

void SAMPLE_TDL_Get_RTSP_Config(CVI_RTSP_CONFIG *pstRTSPConfig) {
  memset(pstRTSPConfig, 0, sizeof(CVI_RTSP_CONFIG));
  pstRTSPConfig->port = 554;
}

PIC_SIZE_E SAMPLE_TDL_Get_PIC_Size(CVI_S32 width, CVI_S32 height) {
  if (width == 1280 && height == 720) {
    return PIC_720P;
  } else if (width == 1920 && height == 1080) {
    return PIC_1080P;
  } else if (width == 3840 && height == 2160) {
    return PIC_3840x2160;
  } else if (width == 2560 && height == 1440) {
    return PIC_1440P;
  } else {
    return PIC_BUTT;
  }
}

CVI_S32 SAMPLE_TDL_Init_WM(SAMPLE_TDL_MW_CONFIG_S *pstMWConfig,
                           SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
  MMF_VERSION_S stVersion;
  CVI_SYS_GetVersion(&stVersion);
  printf("MMF Version:%s\n", stVersion.version);

  if (pstMWConfig->stViConfig.s32WorkingViNum <= 0) {
    printf("Invalidate working vi number: %u\n", pstMWConfig->stViConfig.s32WorkingViNum);
    return CVI_FAILURE;
  }

  // Pre-cleanup: force destroy any stale VPSS groups left by previous runs.
  // Always clean grp 0..1 — grp 1 is used by TDL internally (vpssGrp=1).
  for (CVI_U32 g = 0; g < 2; g++) {
    CVI_VPSS_StopGrp((VPSS_GRP)g);
    CVI_VPSS_DestroyGrp((VPSS_GRP)g);
  }

  // Set sensor number
  CVI_VI_SetDevNum(pstMWConfig->stViConfig.s32WorkingViNum);

  // Setup VB
  if (pstMWConfig->stVBPoolConfig.u32VBPoolCount <= 0 ||
      pstMWConfig->stVBPoolConfig.u32VBPoolCount >= VB_MAX_COMM_POOLS) {
    printf("Invalid number of vb pool: %u, which is outside the valid range of 1 to (%u - 1)\n",
           pstMWConfig->stVBPoolConfig.u32VBPoolCount, VB_MAX_COMM_POOLS);
    return CVI_FAILURE;
  }

  VB_CONFIG_S stVbConf;
  memset(&stVbConf, 0, sizeof(VB_CONFIG_S));

  stVbConf.u32MaxPoolCnt = pstMWConfig->stVBPoolConfig.u32VBPoolCount;
  CVI_U32 u32TotalBlkSize = 0;
  for (uint32_t u32VBIndex = 0; u32VBIndex < stVbConf.u32MaxPoolCnt; u32VBIndex++) {
    CVI_U32 u32BlkSize =
        COMMON_GetPicBufferSize(pstMWConfig->stVBPoolConfig.astVBPoolSetup[u32VBIndex].u32Width,
                                pstMWConfig->stVBPoolConfig.astVBPoolSetup[u32VBIndex].u32Height,
                                pstMWConfig->stVBPoolConfig.astVBPoolSetup[u32VBIndex].enFormat,
                                DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
    stVbConf.astCommPool[u32VBIndex].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[u32VBIndex].u32BlkCnt =
        pstMWConfig->stVBPoolConfig.astVBPoolSetup[u32VBIndex].u32BlkCount;
    CVI_U32 u32PoolSize = (u32BlkSize * stVbConf.astCommPool[u32VBIndex].u32BlkCnt);
    u32TotalBlkSize += u32PoolSize;
    printf("Create VBPool[%u], size: (%u * %u) = %u bytes\n", u32VBIndex, u32BlkSize,
           stVbConf.astCommPool[u32VBIndex].u32BlkCnt, u32PoolSize);
  }
  printf("Total memory of VB pool: %u bytes\n", u32TotalBlkSize);

  const char *forceManualViInit = getenv("SAMPLE_TDL_FORCE_MANUAL_VI_INIT");
  const char *forcePlatViInit = getenv("SAMPLE_TDL_FORCE_PLAT_VI_INIT");
  const char *skipUserVb = getenv("SAMPLE_TDL_SKIP_USER_VB");

  // SYS init: DefaultConfig (called inside SAMPLE_PLAT_VI_INIT) handles SAMPLE_PLAT_SYS_INIT.
  // We only call SAMPLE_COMM_SYS_Init for the manual path.
  CVI_S32 s32Ret;
  printf("Initialize SYS and VB\n");
  if (skipUserVb && strcmp(skipUserVb, "1") == 0) {
    printf("SAMPLE_TDL_SKIP_USER_VB=1: skipping user VB pool setup, using PLAT_SYS_INIT pool\n");
  } else if (forceManualViInit && strcmp(forceManualViInit, "1") == 0) {
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (s32Ret != CVI_SUCCESS) {
      printf("system init failed with %#x\n", s32Ret);
      return s32Ret;
    }
  }

  // Init VI
  printf("Initialize VI\n");
  VI_VPSS_MODE_S stVIVPSSMode;
  memset(&stVIVPSSMode, 0, sizeof(stVIVPSSMode));
  const char *pszViVpssMode = getenv("SAMPLE_TDL_VI_VPSS_MODE");
  VI_VPSS_MODE_E enViVpssMode = VI_OFFLINE_VPSS_ONLINE;
  if (pszViVpssMode && strcmp(pszViVpssMode, "online") == 0) {
    enViVpssMode = VI_ONLINE_VPSS_ONLINE;
    printf("VI_VPSS_MODE: VI_ONLINE_VPSS_ONLINE\n");
  } else if (pszViVpssMode && strcmp(pszViVpssMode, "offline_offline") == 0) {
    enViVpssMode = VI_OFFLINE_VPSS_OFFLINE;
    printf("VI_VPSS_MODE: VI_OFFLINE_VPSS_OFFLINE\n");
  } else {
    printf("VI_VPSS_MODE: VI_OFFLINE_VPSS_ONLINE (default)\n");
  }
  stVIVPSSMode.aenMode[0] = enViVpssMode;
  CVI_SYS_SetVIVPSSMode(&stVIVPSSMode);

#ifndef CV186X
  /* Set VPSS mode BEFORE VI/ISP init — driver may lock mode once ISP starts.
   * We set it here (pre-VI) AND again after VI for safety. */
  {
    VPSS_MODE_S stVPSSModePreVI = pstMWConfig->stVPSSPoolConfig.stVpssMode;
    CVI_S32 vpss_pre_ret = CVI_SYS_SetVPSSModeEx(&stVPSSModePreVI);
    printf("SetVPSSModeEx (pre-VI): enMode=%d input[0]=%d input[1]=%d ret=%#x\n",
           stVPSSModePreVI.enMode, stVPSSModePreVI.aenInput[0], stVPSSModePreVI.aenInput[1],
           vpss_pre_ret);
  }
#endif

  memcpy(&pstMWContext->stViConfig, &pstMWConfig->stViConfig, sizeof(SAMPLE_VI_CONFIG_S));

  if (forceManualViInit && strcmp(forceManualViInit, "1") == 0) {
    printf("Initialize VI via manual diagnostic path\n");
    s32Ret = SAMPLE_TDL_VI_Init_Manual(&pstMWConfig->stViConfig);
  } else {
    /* DefaultConfig initialises internal sensor state then SAMPLE_PLAT_SYS_INIT runs once.
     * SAMPLE_PLAT_VI_INIT uses that state to bring up MIPI + ISP + VI channels. */
    printf("Initialize VI via DefaultConfig + SAMPLE_PLAT_VI_INIT\n");
    s32Ret = SAMPLE_COMM_VI_DefaultConfig();
    if (s32Ret != CVI_SUCCESS) {
      printf("DefaultConfig ret=0x%x (non-fatal, continuing)\n", s32Ret);
    }
    s32Ret = SAMPLE_PLAT_VI_INIT(&pstMWConfig->stViConfig);
  }

  if (s32Ret != CVI_SUCCESS) {
    printf("vi init failed. s32Ret: 0x%x !\n", s32Ret);
    goto vi_start_error;
  }
  
  const char *forceEnableAllViChn = getenv("SAMPLE_TDL_FORCE_ENABLE_ALL_VI_CHN");
  if (forceEnableAllViChn && strcmp(forceEnableAllViChn, "1") == 0) {
    for (CVI_S32 i = 0; i < pstMWConfig->stViConfig.s32WorkingViNum; i++) {
      VI_PIPE viPipe = pstMWConfig->stViConfig.astViInfo[i].stPipeInfo.aPipe[0];
      if (viPipe >= 0) {
        for (int chn = 0; chn < 4; chn++) {
          CVI_S32 s32ChnRet = CVI_VI_EnableChn(viPipe, chn);
          if (s32ChnRet == CVI_SUCCESS) {
            printf("[VI] EnableChn(pipe=%d, chn=%d) success\n", viPipe, chn);
          } else if (chn == 0) {
            printf("[VI] EnableChn(pipe=%d, chn=%d) failed ret=0x%x\n", viPipe, chn,
                   s32ChnRet);
          }
        }
      }
    }
  } else {
    printf("Skip force-enable VI channels (set SAMPLE_TDL_FORCE_ENABLE_ALL_VI_CHN=1 to enable)\n");
  }
  
  if (!(forceManualViInit && strcmp(forceManualViInit, "1") == 0)) {
    ISP_PUB_ATTR_S stPubAttr = {0};
    CVI_ISP_GetPubAttr(0, &stPubAttr);
    stPubAttr.f32FrameRate = 30;
    CVI_ISP_SetPubAttr(0, &stPubAttr);
  } else {
    printf("Skip ISP pub-attr tweak in manual mode\n");
  }
  // Init VPSS
  printf("Initialize VPSS\n");
  memcpy(&pstMWContext->stVPSSPoolConfig, &pstMWConfig->stVPSSPoolConfig,
         sizeof(SAMPLE_TDL_VPSS_POOL_CONFIG_S));

  const char *usePlatVpss = getenv("SAMPLE_TDL_USE_PLAT_VPSS");
  if (usePlatVpss && strcmp(usePlatVpss, "1") == 0) {
    /* Use SAMPLE_PLAT_VPSS_INIT — same as factory sample_vio.
     * Only init grp 0 with sensor size in, first chn size out. */
    SAMPLE_TDL_VPSS_CONFIG_S *pstVPSSConf0 = &pstMWConfig->stVPSSPoolConfig.astVpssConfig[0];
    SIZE_S stSizeIn = {
        .u32Width  = pstVPSSConf0->stVpssGrpAttr.u32MaxW,
        .u32Height = pstVPSSConf0->stVpssGrpAttr.u32MaxH,
    };
    SIZE_S stSizeOut = {
        .u32Width  = pstVPSSConf0->astVpssChnAttr[0].u32Width,
        .u32Height = pstVPSSConf0->astVpssChnAttr[0].u32Height,
    };
    printf("SAMPLE_PLAT_VPSS_INIT: grp=0 in=%ux%u out=%ux%u\n",
           stSizeIn.u32Width, stSizeIn.u32Height, stSizeOut.u32Width, stSizeOut.u32Height);
    s32Ret = SAMPLE_PLAT_VPSS_INIT(0, stSizeIn, stSizeOut);
    if (s32Ret != CVI_SUCCESS) {
      printf("SAMPLE_PLAT_VPSS_INIT failed: 0x%x\n", s32Ret);
      goto vpss_start_error;
    }
    if (pstVPSSConf0->bBindVI) {
      printf("Bind VI with VPSS Grp(0), Chn(%u)\n", pstVPSSConf0->u32ChnBindVI);
      s32Ret = SAMPLE_COMM_VI_Bind_VPSS(0, pstVPSSConf0->u32ChnBindVI, 0);
      if (s32Ret != CVI_SUCCESS) {
        printf("vi bind vpss failed. s32Ret: 0x%x !\n", s32Ret);
        goto vpss_start_error;
      }
    }
    goto vpss_done;
  }

#ifndef CV186X
  {
    VPSS_MODE_S stVPSSMode = pstMWConfig->stVPSSPoolConfig.stVpssMode;
    CVI_S32 vpss_ret = CVI_SYS_SetVPSSModeEx(&stVPSSMode);
    printf("SetVPSSModeEx (post-VI, confirm): enMode=%d input[0]=%d input[1]=%d ret=%#x\n",
           stVPSSMode.enMode, stVPSSMode.aenInput[0], stVPSSMode.aenInput[1], vpss_ret);
    VPSS_MODE_S stCurrentMode;
    memset(&stCurrentMode, 0, sizeof(stCurrentMode));
    CVI_SYS_GetVPSSModeEx(&stCurrentMode);
    printf("GetVPSSModeEx: enMode=%d input[0]=%d input[1]=%d\n",
           stCurrentMode.enMode, stCurrentMode.aenInput[0], stCurrentMode.aenInput[1]);
  }
#endif

  for (uint32_t u32VpssGrpIndex = 0;
       u32VpssGrpIndex < pstMWConfig->stVPSSPoolConfig.u32VpssGrpCount; u32VpssGrpIndex++) {
    SAMPLE_TDL_VPSS_CONFIG_S *pstVPSSConf =
        &pstMWConfig->stVPSSPoolConfig.astVpssConfig[u32VpssGrpIndex];
    VPSS_GRP_ATTR_S *pstGrpAttr = &pstVPSSConf->stVpssGrpAttr;
    VPSS_CHN_ATTR_S *pastChnAttr = pstVPSSConf->astVpssChnAttr;
    printf("---------VPSS[%u]---------\n", u32VpssGrpIndex);
    printf("Input size: (%ux%u)\n", pstGrpAttr->u32MaxW, pstGrpAttr->u32MaxH);
    printf("Input format: (%d)\n", pstGrpAttr->enPixelFormat);
#ifndef CV186X
    printf("VPSS physical device number: %u\n", pstGrpAttr->u8VpssDev);
#endif
    printf("Src Frame Rate: %d\n", pstGrpAttr->stFrameRate.s32SrcFrameRate);
    printf("Dst Frame Rate: %d\n", pstGrpAttr->stFrameRate.s32DstFrameRate);

    CVI_BOOL abChnEnable[VPSS_MAX_PHY_CHN_NUM + 1] = {0};
    for (uint32_t u32ChnIndex = 0; u32ChnIndex < pstVPSSConf->u32ChnCount; u32ChnIndex++) {
      abChnEnable[u32ChnIndex] = true;
      printf("    --------CHN[%u]-------\n", u32ChnIndex);
      printf("    Output size: (%ux%u)\n", pastChnAttr[u32ChnIndex].u32Width,
             pastChnAttr[u32ChnIndex].u32Height);
      printf("    Depth: %u\n", pastChnAttr[u32ChnIndex].u32Depth);
      printf("    Do normalization: %d\n", pastChnAttr[u32ChnIndex].stNormalize.bEnable);
      if (pastChnAttr[u32ChnIndex].stNormalize.bEnable) {
        printf("        factor=[%f, %f, %f]\n", pastChnAttr[u32ChnIndex].stNormalize.factor[0],
               pastChnAttr[u32ChnIndex].stNormalize.factor[1],
               pastChnAttr[u32ChnIndex].stNormalize.factor[2]);
        printf("        mean=[%f, %f, %f]\n", pastChnAttr[u32ChnIndex].stNormalize.mean[0],
               pastChnAttr[u32ChnIndex].stNormalize.mean[1],
               pastChnAttr[u32ChnIndex].stNormalize.mean[2]);
        printf("        rounding=%d\n", pastChnAttr[u32ChnIndex].stNormalize.rounding);
      }
      printf("        Src Frame Rate: %d\n", pastChnAttr[u32ChnIndex].stFrameRate.s32SrcFrameRate);
      printf("        Dst Frame Rate: %d\n", pastChnAttr[u32ChnIndex].stFrameRate.s32DstFrameRate);
      printf("    ----------------------\n");
    }
    printf("------------------------\n");

    s32Ret = SAMPLE_COMM_VPSS_Init(u32VpssGrpIndex, abChnEnable, pstGrpAttr, pastChnAttr);
    if (s32Ret != CVI_SUCCESS) {
      // Recover from stale VPSS group state (0xc0068004 = occupied).
      // Try up to 3 times with increasing delays.
      int vpss_retry;
      for (vpss_retry = 0; vpss_retry < 3 && s32Ret != CVI_SUCCESS; vpss_retry++) {
        printf("init vpss group failed (try %d). s32Ret: 0x%x, retry after destroy+sleep\n",
               vpss_retry + 1, s32Ret);
        CVI_VPSS_StopGrp((VPSS_GRP)u32VpssGrpIndex);
        CVI_VPSS_DestroyGrp((VPSS_GRP)u32VpssGrpIndex);
        usleep((vpss_retry + 1) * 200000); /* 200ms, 400ms, 600ms */
        s32Ret = SAMPLE_COMM_VPSS_Init(u32VpssGrpIndex, abChnEnable, pstGrpAttr, pastChnAttr);
      }
      if (s32Ret != CVI_SUCCESS) {
        printf("init vpss group retry failed after %d tries. s32Ret: 0x%x !\n", vpss_retry,
               s32Ret);
        goto vpss_start_error;
      }
    }

    s32Ret = SAMPLE_COMM_VPSS_Start(u32VpssGrpIndex, abChnEnable, pstGrpAttr, pastChnAttr);
    if (s32Ret != CVI_SUCCESS) {
      printf("start vpss group failed. s32Ret: 0x%x !\n", s32Ret);
      goto vpss_start_error;
    }

    if (pstVPSSConf->bBindVI) {
      printf("Bind VI with VPSS Grp(%u), Chn(%u)\n", u32VpssGrpIndex, pstVPSSConf->u32ChnBindVI);
      s32Ret = SAMPLE_COMM_VI_Bind_VPSS(0, pstVPSSConf->u32ChnBindVI, u32VpssGrpIndex);
      if (s32Ret != CVI_SUCCESS) {
        printf("vi bind vpss failed. s32Ret: 0x%x !\n", s32Ret);
        goto vpss_start_error;
      }
    }
  }

  // Attach VB to VPSS
  if (skipUserVb && strcmp(skipUserVb, "1") == 0) {
    printf("SAMPLE_TDL_SKIP_USER_VB=1: skipping AttachVbPool\n");
  } else {
    for (CVI_U32 u32VBPoolIndex = 0; u32VBPoolIndex < pstMWConfig->stVBPoolConfig.u32VBPoolCount;
         u32VBPoolIndex++) {
      SAMPLE_TDL_VB_CONFIG_S *pstVBConfig =
          &pstMWConfig->stVBPoolConfig.astVBPoolSetup[u32VBPoolIndex];
      if (pstVBConfig->bBind) {
        printf("Attach VBPool(%u) to VPSS Grp(%u) Chn(%u)\n", u32VBPoolIndex,
               pstVBConfig->u32VpssGrpBinding, pstVBConfig->u32VpssChnBinding);

        s32Ret = CVI_VPSS_AttachVbPool(pstVBConfig->u32VpssGrpBinding,
                                       pstVBConfig->u32VpssChnBinding, (VB_POOL)u32VBPoolIndex);
        if (s32Ret != CVI_SUCCESS) {
          printf("Cannot attach VBPool(%u) to VPSS Grp(%u) Chn(%u): ret=%x\n", u32VBPoolIndex,
                 pstVBConfig->u32VpssGrpBinding, pstVBConfig->u32VpssChnBinding, s32Ret);
          goto vpss_start_error;
        }
      }
    }
  }

vpss_done:;
  const char *pszDisableVenc = getenv("SAMPLE_TDL_DISABLE_VENC");
  const char *pszDisableRtsp = getenv("SAMPLE_TDL_DISABLE_RTSP");
  CVI_BOOL bDisableVenc = (pszDisableVenc && strcmp(pszDisableVenc, "1") == 0);
  CVI_BOOL bDisableRtsp = (pszDisableRtsp && strcmp(pszDisableRtsp, "1") == 0);

  if (bDisableVenc) {
    printf("Skip VENC/RTSP by SAMPLE_TDL_DISABLE_VENC=1\n");
    pstMWContext->u32VencChn = (CVI_U32)-1;
    pstMWContext->pstRtspContext = NULL;
    pstMWContext->pstSession = NULL;
    return CVI_SUCCESS;
  }

  // Init VENC
  VENC_GOP_ATTR_S stGopAttr;

  // Always use venc chn0 in sample.
  pstMWContext->u32VencChn = 0;

  SAMPLE_COMM_CHN_INPUT_CONFIG_S *pstInputConfig = &pstMWConfig->stVencConfig.stChnInputCfg;

  printf("Initialize VENC\n");
  s32Ret = SAMPLE_COMM_VENC_GetGopAttr(VENC_GOPMODE_NORMALP, &stGopAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("Venc Get GopAttr for %#x!\n", s32Ret);
    goto venc_start_error;
  }

  printf("venc codec: %s\n", pstInputConfig->codec);
  printf("venc frame size: %ux%u\n", pstMWConfig->stVencConfig.u32FrameWidth,
         pstMWConfig->stVencConfig.u32FrameHeight);

  PAYLOAD_TYPE_E enPayLoad;
  if (!strcmp(pstInputConfig->codec, "h264")) {
    enPayLoad = PT_H264;
  } else if (!strcmp(pstInputConfig->codec, "h265")) {
    enPayLoad = PT_H265;
  } else {
    printf("Unsupported encode format in sample: %s\n", pstInputConfig->codec);
    s32Ret = CVI_FAILURE;
    goto venc_start_error;
  }

  PIC_SIZE_E enPicSize = SAMPLE_TDL_Get_PIC_Size(pstMWConfig->stVencConfig.u32FrameWidth,
                                                 pstMWConfig->stVencConfig.u32FrameHeight);
  if (enPicSize == PIC_BUTT) {
    s32Ret = CVI_FAILURE;
    printf("Cannot get PIC SIZE from VENC frame size: %ux%u\n",
           pstMWConfig->stVencConfig.u32FrameWidth, pstMWConfig->stVencConfig.u32FrameHeight);
    goto venc_start_error;
  }

  s32Ret = SAMPLE_COMM_VENC_Start(pstInputConfig, pstMWContext->u32VencChn, enPayLoad, enPicSize,
                                  pstInputConfig->rcMode, 0, CVI_FALSE, &stGopAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("Venc Start failed for %#x!\n", s32Ret);
    goto venc_start_error;
  }

  if (bDisableRtsp) {
    printf("Skip RTSP by SAMPLE_TDL_DISABLE_RTSP=1\n");
    pstMWContext->pstRtspContext = NULL;
    pstMWContext->pstSession = NULL;
    return CVI_SUCCESS;
  }

  // RTSP
  printf("Initialize RTSP\n");
  if (0 > CVI_RTSP_Create(&pstMWContext->pstRtspContext, &pstMWConfig->stRTSPConfig.stRTSPConfig)) {
    printf("fail to create rtsp context\n");
    s32Ret = CVI_FAILURE;
    goto rtsp_create_error;
  }

  CVI_RTSP_SESSION_ATTR attr = {0};
  if (enPayLoad == PT_H264) {
    attr.video.codec = RTSP_VIDEO_H264;
    snprintf(attr.name, sizeof(attr.name), "h264");
  } else if (enPayLoad == PT_H265) {
    attr.video.codec = RTSP_VIDEO_H265;
    snprintf(attr.name, sizeof(attr.name), "h265");
  } else {
    printf("Unsupported RTSP codec in sample: %d\n", attr.video.codec);
    s32Ret = CVI_FAILURE;
    goto rtsp_create_error;
  }

  CVI_RTSP_CreateSession(pstMWContext->pstRtspContext, &attr, &pstMWContext->pstSession);

  // Set listener to RTSP
  CVI_RTSP_STATE_LISTENER listener;
  listener.onConnect = pstMWConfig->stRTSPConfig.Lisener.onConnect != NULL
                           ? pstMWConfig->stRTSPConfig.Lisener.onConnect
                           : SAMPLE_TDL_RTSP_ON_CONNECT;
  listener.argConn = pstMWContext->pstRtspContext;
  listener.onDisconnect = pstMWConfig->stRTSPConfig.Lisener.onDisconnect != NULL
                              ? pstMWConfig->stRTSPConfig.Lisener.onDisconnect
                              : SAMPLE_TDL_RTSP_ON_DISCONNECT;
  CVI_RTSP_SetListener(pstMWContext->pstRtspContext, &listener);

  if (0 > CVI_RTSP_Start(pstMWContext->pstRtspContext)) {
    printf("Cannot start RTSP\n");
    s32Ret = CVI_FAILURE;
    goto rts_start_error;
  }

  return CVI_SUCCESS;

rts_start_error:
  CVI_RTSP_DestroySession(pstMWContext->pstRtspContext, pstMWContext->pstSession);
  CVI_RTSP_Destroy(&pstMWContext->pstRtspContext);

rtsp_create_error:
  SAMPLE_COMM_VENC_Stop(0);

venc_start_error:
vpss_start_error:
  SAMPLE_COMM_VI_DestroyIsp(&pstMWConfig->stViConfig);
  SAMPLE_COMM_VI_DestroyVi(&pstMWConfig->stViConfig);
vi_start_error:
  SAMPLE_COMM_SYS_Exit();

  return s32Ret;
}

CVI_S32 SAMPLE_TDL_Send_Frame_RTSP(VIDEO_FRAME_INFO_S *stVencFrame,
                                   SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
  CVI_S32 s32Ret = CVI_SUCCESS;

  CVI_S32 s32SetFrameMilliSec = 20000;
  VENC_STREAM_S stStream;
  VENC_CHN_ATTR_S stVencChnAttr;
  VENC_CHN_STATUS_S stStat;
  VENC_CHN VencChn = pstMWContext->u32VencChn;

  s32Ret = CVI_VENC_SendFrame(VencChn, stVencFrame, s32SetFrameMilliSec);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VENC_SendFrame failed! %d\n", s32Ret);
    return s32Ret;
  }

  s32Ret = CVI_VENC_GetChnAttr(VencChn, &stVencChnAttr);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VENC_GetChnAttr, VencChn[%d], s32Ret = %d\n", VencChn, s32Ret);
    return s32Ret;
  }

  s32Ret = CVI_VENC_QueryStatus(VencChn, &stStat);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VENC_QueryStatus failed with %#x!\n", s32Ret);
    return s32Ret;
  }

  if (!stStat.u32CurPacks) {
    printf("NOTE: Current frame is NULL!\n");
    return s32Ret;
  }

  stStream.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
  if (stStream.pstPack == NULL) {
    printf("malloc memory failed!\n");
    return s32Ret;
  }

  s32Ret = CVI_VENC_GetStream(VencChn, &stStream, 10000);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_VENC_GetStream failed with %#x!\n", s32Ret);
    goto send_failed;
  }

  VENC_PACK_S *ppack;
  CVI_RTSP_DATA data = {0};
  memset(&data, 0, sizeof(CVI_RTSP_DATA));

  data.blockCnt = stStream.u32PackCount;
  for (unsigned int i = 0; i < stStream.u32PackCount; i++) {
    ppack = &stStream.pstPack[i];
    data.dataPtr[i] = ppack->pu8Addr + ppack->u32Offset;
    data.dataLen[i] = ppack->u32Len - ppack->u32Offset;
  }

  s32Ret =
      CVI_RTSP_WriteFrame(pstMWContext->pstRtspContext, pstMWContext->pstSession->video, &data);
  if (s32Ret != CVI_SUCCESS) {
    printf("CVI_RTSP_WriteFrame, s32Ret = %d\n", s32Ret);
    goto send_failed;
  }

send_failed:
  CVI_VENC_ReleaseStream(VencChn, &stStream);
  free(stStream.pstPack);
  stStream.pstPack = NULL;
  return s32Ret;
}

void SAMPLE_TDL_Stop_VPSS(SAMPLE_TDL_VPSS_POOL_CONFIG_S *pstVPSSPoolConfig) {
  for (uint32_t u32VpssIndex = 0; u32VpssIndex < pstVPSSPoolConfig->u32VpssGrpCount;
       u32VpssIndex++) {
    printf("stop VPSS (%u)\n", u32VpssIndex);
    CVI_BOOL abChnEnable[VPSS_MAX_PHY_CHN_NUM + 1] = {0};
    for (uint32_t u32VpssChnIndex = 0;
         u32VpssChnIndex < pstVPSSPoolConfig->astVpssConfig[u32VpssIndex].u32ChnCount;
         u32VpssChnIndex++) {
      abChnEnable[u32VpssChnIndex] = true;
    }
    SAMPLE_COMM_VPSS_Stop(u32VpssIndex, abChnEnable);
  }
}

/* UnBind VI from all VPSS groups that were bound, then stop VPSS.
 * Must be called BEFORE destroying ISP/VI so VPSS releases DMA refs
 * to VI_DMA_BUF and ISP_SHARED_BUFFER first. */
static void unbind_and_stop_vpss(SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
  SAMPLE_TDL_VPSS_POOL_CONFIG_S *pCfg = &pstMWContext->stVPSSPoolConfig;
  for (uint32_t i = 0; i < pCfg->u32VpssGrpCount; i++) {
    if (pCfg->astVpssConfig[i].bBindVI) {
      SAMPLE_COMM_VI_UnBind_VPSS(0, pCfg->astVpssConfig[i].u32ChnBindVI, (VPSS_GRP)i);
    }
  }
  SAMPLE_TDL_Stop_VPSS(pCfg);
}

void SAMPLE_TDL_Destroy_MW(SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
  printf("destroy middleware\n");
  if (pstMWContext->pstRtspContext != NULL) {
    CVI_RTSP_Stop(pstMWContext->pstRtspContext);
    if (pstMWContext->pstSession != NULL) {
      CVI_RTSP_DestroySession(pstMWContext->pstRtspContext, pstMWContext->pstSession);
    }
    CVI_RTSP_Destroy(&pstMWContext->pstRtspContext);
  }
  /* Stop VENC receive before touching VPSS/VI */
  if (pstMWContext->u32VencChn != (CVI_U32)-1) {
    CVI_VENC_StopRecvFrame(pstMWContext->u32VencChn);
    CVI_VENC_ResetChn(pstMWContext->u32VencChn);
  }

  /* UnBind VI→VPSS and stop VPSS BEFORE destroying VI/ISP.
   * VPSS holds DMA references to VI_DMA_BUF; releasing them first
   * allows the VI driver to free the ION buffers on DestroyVi. */
  unbind_and_stop_vpss(pstMWContext);

  /* Destroy VENC channel after VPSS stopped */
  if (pstMWContext->u32VencChn != (CVI_U32)-1) {
    CVI_VENC_DestroyChn(pstMWContext->u32VencChn);
  }

  /* VI/ISP teardown — ISP must exit BEFORE pipe ops to avoid write-after-free.
   * In ONLINE VPSS mode CVI_VI_DisableChn is normally skipped by the SDK wrapper
   * (offline-only guard), so we call it directly; but ISP_Exit must come first
   * to stop the ISP writing into the pipe buffers. */
  SAMPLE_COMM_VI_DestroyIsp(&pstMWContext->stViConfig); /* CVI_ISP_Exit first */
  for (CVI_S32 i = 0; i < pstMWContext->stViConfig.s32WorkingViNum; i++) {
    SAMPLE_VI_INFO_S *pInfo = &pstMWContext->stViConfig.astViInfo[i];
    VI_PIPE ViPipe = pInfo->stPipeInfo.aPipe[0];
    CVI_VI_DisableChn(ViPipe, 0);      /* triggers _cvi_vi_freeIonBuf → VI_DMA_BUF */
    SAMPLE_COMM_VI_StopViPipe(pInfo);  /* CVI_VI_StopPipe */
    CVI_VI_DestroyPipe(ViPipe);        /* frees vi_cmdq_buf */
  }
  SAMPLE_COMM_VI_DestroyVi(&pstMWContext->stViConfig);  /* CVI_VI_DisableDev */

  if (g_s32Gc2083I2cFd >= 0) {
    SAMPLE_COMM_I2C_Close(g_s32Gc2083I2cFd);
    g_s32Gc2083I2cFd = -1;
  }

  /* VB_Exit BEFORE SYS_Exit — free VB pool before system exit */
  CVI_VB_Exit();
  CVI_SYS_Exit();
}

void SAMPLE_TDL_Destroy_MW_NO_RTSP(SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
  printf("destroy middleware\n");

  /* Same ordering: UnBind+Stop VPSS before destroying VI/ISP */
  unbind_and_stop_vpss(pstMWContext);

  SAMPLE_COMM_VI_DestroyIsp(&pstMWContext->stViConfig); /* ISP_Exit first */
  for (CVI_S32 i = 0; i < pstMWContext->stViConfig.s32WorkingViNum; i++) {
    SAMPLE_VI_INFO_S *pInfo = &pstMWContext->stViConfig.astViInfo[i];
    VI_PIPE ViPipe = pInfo->stPipeInfo.aPipe[0];
    CVI_VI_DisableChn(ViPipe, 0);
    SAMPLE_COMM_VI_StopViPipe(pInfo);
    CVI_VI_DestroyPipe(ViPipe);
  }
  SAMPLE_COMM_VI_DestroyVi(&pstMWContext->stViConfig);

  if (g_s32Gc2083I2cFd >= 0) {
    SAMPLE_COMM_I2C_Close(g_s32Gc2083I2cFd);
    g_s32Gc2083I2cFd = -1;
  }

  /* VB_Exit BEFORE SYS_Exit */
  CVI_VB_Exit();
  CVI_SYS_Exit();
}
