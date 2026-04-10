#!/bin/bash
# Build script to compile sample_vi_fd on Milk-V Duo device
# with A1-A3 workarounds implemented
#
# This script assumes you have the sources copied to the device and
# a working build environment (gcc, make, libraries)

set -e

echo "====== Building sample_vi_fd with A1/A2/A3 Workarounds ======"

# Configuration - adjust these paths if needed
SOURCE_DIR="/mnt/build"  # Where you copied the source files
OUTPUT_BIN="$SOURCE_DIR/sample_vi_fd_new"
LIBS_DIR="/mnt/shared/duo-tdl-examples/libs"
INCLUDE_SYSTEM="/mnt/shared/duo-tdl-examples/include/system"
INCLUDE_TDL="/mnt/shared/duo-tdl-examples/include/tdl"

# Only needed if cross-compiling (not needed on native device)
# export TOOLCHAIN_PREFIX=""
# export CC="gcc"

# Source file list
SOURCES=(
    "$SOURCE_DIR/sample_vi_fd.c"
    "$SOURCE_DIR/middleware_utils.c"
    "$SOURCE_DIR/sample_utils.c"
    "$SOURCE_DIR/vi_vo_utils.c"
)

# Build flags
CFLAGS=(
    "-O2"
    "-DNDEBUG"
   "-DCV181X"
    "-D__CV181X__"
    "-std=gnu11"
    "-Wno-pointer-to-int-cast"
    "-fsigned-char"
    "-fdiagnostics-color=always"
)

INCLUDES=(
    "-I$INCLUDE_SYSTEM"
    "-I$INCLUDE_TDL"
)

LDFLAGS=(
    "-L$LIBS_DIR/tdl/cv181x_riscv64"
    "-L$LIBS_DIR/system/musl_riscv64"
    "-lpthread"
    "-latomic"
    "-lcvi_vb"
    "-lcvi_sys"
    "-lcvi_vi"
    "-lcvi_vpss"
    "-lcvi_venc"
    "-lcvi_bin"
    "-lcvi_isp"
    "-lcvi_ae"
    "-lcvi_awb"
    "-lcvi_af"
    "-lcvi_tdl"
    "-lcvi_tdl_service"
    "-lm"
)

# Compile
echo "[*] Compiling with RISC-V native compiler..."
echo "    Sources: ${SOURCES[@]}"
echo "    Output: $OUTPUT_BIN"

gcc "${CFLAGS[@]}" "${INCLUDES[@]}" "${SOURCES[@]}" -o "$OUTPUT_BIN" "${LDFLAGS[@]}"

if [ -f "$OUTPUT_BIN" ]; then
    size=$(ls -lh "$OUTPUT_BIN" | awk '{print $5}')
    echo "[OK] Build successful!"
    echo "     Binary: $OUTPUT_BIN ($size)"
    echo ""
    echo "To test the new binary:"
    echo "    export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd"
    echo "    export SAMPLE_TDL_DISABLE_RTSP=1"
    echo "    $OUTPUT_BIN /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel"
else
    echo "[ERROR] Build failed!"
    exit 1
fi
