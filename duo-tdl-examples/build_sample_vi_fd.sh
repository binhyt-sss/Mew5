#!/usr/bin/env bash
set -euo pipefail

cd /mnt/host/d/Mew5/duo-tdl-examples

export TOOLCHAIN_PREFIX=/mnt/host/d/Mew5/duo-tdl-examples/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
export CC=${TOOLCHAIN_PREFIX}gcc
export CFLAGS="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O3 -DNDEBUG -I/mnt/host/d/Mew5/duo-tdl-examples/include/system -I/mnt/host/d/Mew5/duo-tdl-examples/include/tdl"
export LDFLAGS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -L/mnt/host/d/Mew5/duo-tdl-examples/libs/system/musl_riscv64 -L/mnt/host/d/Mew5/duo-tdl-examples/libs/tdl/cv181x_riscv64"
export CHIP=CV181X
export COMMON_DIR=/mnt/host/d/Mew5/duo-tdl-examples/common

make -C sample_vi_fd clean
make -C sample_vi_fd

echo BUILD_OK
