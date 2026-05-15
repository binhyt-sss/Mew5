#!/bin/bash
set -e

# Install cross-compiler if missing
if ! command -v riscv64-linux-gnu-gcc &>/dev/null; then
    echo "[+] Installing RISC-V cross-compiler..."
    sudo apt-get install -y gcc-riscv64-linux-gnu g++-riscv64-linux-gnu
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-riscv"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$SCRIPT_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/toolchain-riscv64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_RISCV=ON

make -j$(nproc) audio_wakeword_linux

echo ""
echo "[OK] Binary: $BUILD_DIR/audio_wakeword_linux"
echo "[?]  file $BUILD_DIR/audio_wakeword_linux"
file "$BUILD_DIR/audio_wakeword_linux"
echo ""
echo "Deploy to board:"
echo "  scp $BUILD_DIR/audio_wakeword_linux root@192.168.42.1:/mnt/data/"
