#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SDK_ROOT=${SDK_ROOT:-$(cd -- "$ROOT/../.." && pwd)}
SAMPLE_DIR="$ROOT/sample_vi_hand_gesture"
COMMON_DIR="$SDK_ROOT/tdl_sdk/install/sample/utils"
LCD_DIR="$SDK_ROOT/Mew5/st7789_display"
SDK_OUT="$SDK_ROOT/install/soc_sg2002_milkv_duo256m_musl_riscv64_sd"
TPU_SDK="$SDK_OUT/tpu_musl_riscv64/cvitek_tpu_sdk"
TOOLCHAIN_PREFIX="$SDK_ROOT/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-"

cd "$SAMPLE_DIR"

export TOOLCHAIN_PREFIX
export CC="${TOOLCHAIN_PREFIX}gcc"
export CFLAGS="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O3 -DNDEBUG -D_MIDDLEWARE_V2_ -I${COMMON_DIR} -I${SDK_ROOT}/tdl_sdk/install/include/cvi_tdl -I${SDK_ROOT}/tdl_sdk/install/include -I${SDK_ROOT}/cvi_mpi/include/isp/cv181x -I${SDK_ROOT}/cvi_mpi/component/isp/common -I${SDK_ROOT}/cvi_mpi/include -I${SDK_ROOT}/cvi_mpi/include/linux -I${SDK_ROOT}/cvi_mpi/3rdparty/inih -I${SDK_ROOT}/cvi_mpi/sample/common -I${SDK_ROOT}/cvi_mpi/sample_app/common -I${SDK_ROOT}/cvi_mpi/component/panel/cv181x -I${SDK_ROOT}/cvi_rtsp/install/include/cvi_rtsp -I${TPU_SDK}/include -I${TPU_SDK}/include/cvikernel"
export LDFLAGS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -L${SDK_ROOT}/cvi_mpi/lib -L${SDK_ROOT}/cvi_mpi/lib/3rd -L${SDK_ROOT}/tdl_sdk/install/lib -L${TPU_SDK}/opencv/lib -L${SDK_OUT}/rootfs/mnt/system/lib -L${TPU_SDK}/lib -L${SDK_ROOT}/cvi_rtsp/install/lib -L${SDK_OUT}/rootfs/mnt/system/usr/lib -Wl,-rpath-link,${TPU_SDK}/opencv/lib -Wl,-rpath-link,${SDK_OUT}/rootfs/mnt/system/lib -Wl,-rpath-link,${TPU_SDK}/lib"
export CHIP=CV181X
export COMMON_DIR="$COMMON_DIR"
export LCD_DIR="$LCD_DIR"

printf 'SDK_ROOT=%s\n' "$SDK_ROOT"
printf 'CC=%s\n' "$CC"
printf 'CFLAGS=%s\n' "$CFLAGS"
printf 'LDFLAGS=%s\n' "$LDFLAGS"
printf 'COMMON_DIR=%s\n' "$COMMON_DIR"
printf 'LCD_DIR=%s\n' "$LCD_DIR"

make clean
make

printf 'BUILD_OK\n'
ls -lh "$SAMPLE_DIR/sample_vi_hand_gesture"
