#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
typedef int CVI_S32;
typedef struct { char data[808]; } SAMPLE_VI_CONFIG_S;
CVI_S32 SAMPLE_PLAT_VI_INIT(SAMPLE_VI_CONFIG_S *pstViConfig) {
    static CVI_S32 (*real_fn)(SAMPLE_VI_CONFIG_S*) = NULL;
    if (!real_fn) real_fn = dlsym(RTLD_NEXT, "SAMPLE_PLAT_VI_INIT");
    const unsigned char *p = (const unsigned char*)pstViConfig;
    printf("FACTORY_VI_CONFIG hex (808 bytes):\n");
    for (int i = 0; i < 808; i += 16) {
        printf("  %03x:", i);
        for (int j = i; j < i+16 && j < 808; j++) printf(" %02x", p[j]);
        printf("\n");
    }
    fflush(stdout);
    return real_fn(pstViConfig);
}
