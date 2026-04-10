/* sample_lcd_test: grab one VPSS frame and display on ST7789 LCD.
 * No TDL, no inference — just camera → LCD. */

#define LOG_TAG "LcdTest"
#define LOG_LEVEL LOG_LEVEL_INFO

#include "middleware_utils.h"
#include "sample_utils.h"

#include <core/utils/vpss_helper.h>
#include <cvi_comm.h>
#include <sample_comm.h>
#include <cvi_sys.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "st7789.h"

static volatile int g_running = 1;
static void handle_sig(int s) { (void)s; g_running = 0; }

static CVI_S32 get_mw_config(SAMPLE_TDL_MW_CONFIG_S *cfg) {
    CVI_S32 ret = SAMPLE_TDL_Get_VI_Config(&cfg->stViConfig);
    if (ret != CVI_SUCCESS || cfg->stViConfig.s32WorkingViNum <= 0) {
        printf("Failed to get sensor info\n");
        return -1;
    }

    PIC_SIZE_E enPicSize;
    ret = SAMPLE_COMM_VI_GetSizeBySensor(
        cfg->stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
    if (ret != CVI_SUCCESS) return ret;

    SIZE_S stSensorSize;
    ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
    if (ret != CVI_SUCCESS) return ret;

    if (stSensorSize.u32Width < 1920) {
        stSensorSize.u32Width  = 1920;
        stSensorSize.u32Height = 1080;
        printf("Override sensor size → 1920x1080\n");
    }

    SIZE_S stOutSize = { .u32Width = 640, .u32Height = 480 };

    /* Minimal VB pool — 1 small NV21 pool */
    cfg->stVBPoolConfig.u32VBPoolCount = 1;
    cfg->stVBPoolConfig.astVBPoolSetup[0].enFormat = PIXEL_FORMAT_NV21;
    cfg->stVBPoolConfig.astVBPoolSetup[0].u32Width  = stOutSize.u32Width;
    cfg->stVBPoolConfig.astVBPoolSetup[0].u32Height = stOutSize.u32Height;
    cfg->stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = 2;
    cfg->stVBPoolConfig.astVBPoolSetup[0].bBind = false;

    cfg->stVPSSPoolConfig.u32VpssGrpCount = 1;
#ifndef CV186X
    cfg->stVPSSPoolConfig.stVpssMode.ViPipe[0] = 0;
    cfg->stVPSSPoolConfig.stVpssMode.ViPipe[1] = 0;
    cfg->stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_DUAL;
    cfg->stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
    cfg->stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;
#endif

    SAMPLE_TDL_VPSS_CONFIG_S *vpss = &cfg->stVPSSPoolConfig.astVpssConfig[0];
    vpss->bBindVI = true;
    vpss->u32ChnBindVI = 0;
    VPSS_GRP_DEFAULT_HELPER2(&vpss->stVpssGrpAttr,
                             stSensorSize.u32Width, stSensorSize.u32Height,
                             VI_PIXEL_FORMAT, 1);
    vpss->u32ChnCount = 1;
    VPSS_CHN_DEFAULT_HELPER(&vpss->astVpssChnAttr[0],
                            stOutSize.u32Width, stOutSize.u32Height,
                            VI_PIXEL_FORMAT, true);
    vpss->astVpssChnAttr[0].u32Depth = 2;

    /* Disable VENC/RTSP */
    cfg->stVencConfig.u32FrameWidth  = 0;
    cfg->stVencConfig.u32FrameHeight = 0;

    return CVI_SUCCESS;
}

int main(void) {
    signal(SIGINT,  handle_sig);
    signal(SIGTERM, handle_sig);

    /* Init LCD first so we know it's alive */
    if (st7789_open() != 0) {
        printf("ST7789 init failed\n");
        return 1;
    }
    st7789_fill(0xF800);  /* red — screen alive indicator */
    printf("ST7789 ready — screen should be RED\n");
    sleep(1);
    st7789_fill(0x0000);

    /* Init middleware (VI + VPSS) */
    SAMPLE_TDL_MW_CONFIG_S cfg = {0};
    if (get_mw_config(&cfg) != CVI_SUCCESS) {
        printf("get_mw_config failed\n");
        return 1;
    }

    setenv("SAMPLE_TDL_USE_PLAT_VPSS", "1", 1);
    setenv("SAMPLE_TDL_DISABLE_RTSP",  "1", 1);

    SAMPLE_TDL_MW_CONTEXT ctx = {0};
    if (SAMPLE_TDL_Init_WM(&cfg, &ctx) != CVI_SUCCESS) {
        printf("SAMPLE_TDL_Init_WM failed\n");
        return 1;
    }

    printf("VI+VPSS ready. Streaming to LCD... (Ctrl+C to stop)\n");

    int frame_count = 0;
    while (g_running) {
        VIDEO_FRAME_INFO_S stFrame;
        CVI_S32 ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN0, &stFrame, 2000);
        if (ret != CVI_SUCCESS) {
            printf("GetChnFrame failed: %#x\n", ret);
            usleep(100000);
            continue;
        }

        VIDEO_FRAME_S *vf = &stFrame.stVFrame;
        if (frame_count < 3)
            printf("Frame[%d]: %dx%d stride=%d fmt=%d phys=0x%llx\n",
                   frame_count, vf->u32Width, vf->u32Height,
                   vf->u32Stride[0], vf->enPixelFormat,
                   (unsigned long long)vf->u64PhyAddr[0]);

        CVI_U32 frame_size = vf->u32Stride[0] * vf->u32Height * 3 / 2;
        CVI_VOID *vaddr = CVI_SYS_Mmap(vf->u64PhyAddr[0], frame_size);
        if (vaddr) {
            st7789_draw_nv21((const uint8_t *)vaddr,
                             (int)vf->u32Width,
                             (int)vf->u32Height,
                             (int)vf->u32Stride[0]);
            CVI_SYS_Munmap(vaddr, frame_size);
        }

        CVI_VPSS_ReleaseChnFrame(0, VPSS_CHN0, &stFrame);
        frame_count++;

        if (frame_count % 30 == 0)
            printf("Frames displayed: %d\n", frame_count);
    }

    printf("Stopping...\n");
    st7789_fill(0x0000);
    st7789_close();
    SAMPLE_TDL_Destroy_MW(&ctx);
    return 0;
}
