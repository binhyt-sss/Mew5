#!/usr/bin/env bash
set -euo pipefail

if [ -d /mnt/d/Mew5/duo-tdl-examples ]; then
    ROOT=/mnt/d/Mew5/duo-tdl-examples
else
    ROOT=/mnt/host/d/Mew5/duo-tdl-examples
fi
SAMPLE_DIR="$ROOT/sample_lcd_test"
COMMON_DIR="$ROOT/common"
TOOLCHAIN_PREFIX="$ROOT/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-"

cd "$SAMPLE_DIR"

export TOOLCHAIN_PREFIX
export CC="${TOOLCHAIN_PREFIX}gcc"
export CFLAGS="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O2 -DNDEBUG -I${ROOT}/include/system -I${ROOT}/include/tdl"
export LDFLAGS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -L${ROOT}/libs/system/musl_riscv64 -L${ROOT}/libs/tdl/cv181x_riscv64"
export CHIP=CV181X
export COMMON_DIR="$COMMON_DIR"

make clean
make

printf 'BUILD_OK\n'
ls -lh "$SAMPLE_DIR/sample_lcd_test"
