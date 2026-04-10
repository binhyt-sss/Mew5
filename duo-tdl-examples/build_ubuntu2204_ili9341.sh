#!/usr/bin/env bash
set -euo pipefail

if [ -d /mnt/d/Mew5/duo-tdl-examples ]; then
    ROOT=/mnt/d/Mew5/duo-tdl-examples
else
    ROOT=/mnt/host/d/Mew5/duo-tdl-examples
fi

TOOLCHAIN_PREFIX="$ROOT/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-"
SRC_DIR="/mnt/d/Mew5/st7789_display"

CC="${TOOLCHAIN_PREFIX}gcc"
CFLAGS="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O2"

echo "Building ili9341_color_test..."
$CC $CFLAGS -o "$SRC_DIR/ili9341_color_test" "$SRC_DIR/ili9341_color_test.c"
echo "BUILD_OK: ili9341_color_test"

echo "Building ili9341_show (with stb_image, may take ~30s)..."
$CC $CFLAGS -o "$SRC_DIR/ili9341_show" "$SRC_DIR/ili9341.c"
echo "BUILD_OK: ili9341_show"

ls -lh "$SRC_DIR/ili9341_color_test" "$SRC_DIR/ili9341_show"
