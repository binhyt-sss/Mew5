#!/bin/bash
set -e

cd /mnt/d/Mew5/duo-tdl-examples/sample_vi_vpss_probe

export TOOLCHAIN_PREFIX=/mnt/d/Mew5/duo-tdl-examples/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
export CC="${TOOLCHAIN_PREFIX}gcc"
export CFLAGS="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O3 -DNDEBUG -I/mnt/d/Mew5/duo-tdl-examples/include/system -I/mnt/d/Mew5/duo-tdl-examples/include/tdl"
export LDFLAGS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -L/mnt/d/Mew5/duo-tdl-examples/libs/system/musl_riscv64 -L/mnt/d/Mew5/duo-tdl-examples/libs/tdl/cv181x_riscv64"
export CHIP=CV181X
export COMMON_DIR=/mnt/d/Mew5/duo-tdl-examples/common

echo "CC=$CC"
echo "Building..."
make clean
make -j4
echo "Build complete"
ls -lh sample_vi_vpss_probe
