#!/usr/bin/env python3
"""
ONE-COMMAND BUILD: sample_vi_fd with A1/A2/A3 workarounds
Works on: Windows (Docker Desktop), Mac, Linux
No prerequisites: Just Docker

Usage:
    python3 docker_build.py
    
Or manually:
    docker run --rm -v D:/Mew5:/workspace -w /workspace ubuntu:22.04 bash -c '
    apt-get update && apt-get install -y build-essential 2>&1 | grep -i "done\|setting"
    cd duo-tdl-examples/sample_vi_fd
    make clean 2>/dev/null
    export TOOLCHAIN_PREFIX=/workspace/duo-tdl-examples/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
    export CC=$TOOLCHAIN_PREFIX"gcc"
    export CFLAGS="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O3 -DNDEBUG -I/workspace/duo-tdl-examples/include/system -I/workspace/duo-tdl-examples/include/tdl"
    export LDFLAGS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -L/workspace/duo-tdl-examples/libs/system/musl_riscv64 -L/workspace/duo-tdl-examples/libs/tdl/cv181x_riscv64"
    export CHIP=CV181X
    export COMMON_DIR=/workspace/duo-tdl-examples/common
    make 2>&1
    if [ -f sample_vi_fd ]; then
        echo "BUILD SUCCESS: $(ls -lh sample_vi_fd | awk \"{print \\$5}\")"
        cp sample_vi_fd /workspace/duo-tdl-examples/sample_vi_fd/sample_vi_fd.built
    else
        echo "BUILD FAILED"
        exit 1
    fi
    '
"""

import subprocess
import sys
import os
import platform

def main():
    workspace = "D:/Mew5" if platform.system() == "Windows" else os.path.expanduser("~/Mew5")
    
    # Check if Docker is available
    try:
        subprocess.run(["docker", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("ERROR: Docker not installed. Install from: https://www.docker.com/products/docker-desktop")
        print("\nAlternatively, follow FINAL_BUILD_GUIDE.md Option 2 (Linux) or Option 3 (CI)")
        return 1
    
    print("="*70)
    print("BUILD sample_vi_fd with A1/A2/A3 Workarounds - Docker Method")
    print("="*70)
    print()
    
    # Build command
    cmd = [
        "docker", "run", "--rm",
        "-v", f"{workspace}:/workspace",
        "-w", "/workspace",
        "ubuntu:22.04",
        "bash", "-c",
        """
apt-get update -qq && apt-get install -y build-essential 2>&1 | tail -1
cd duo-tdl-examples/sample_vi_fd
echo "[BUILD] Compiling sample_vi_fd with A1/A2/A3 workarounds..."
export TOOLCHAIN_PREFIX=/workspace/duo-tdl-examples/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
export CC=${TOOLCHAIN_PREFIX}gcc
export CFLAGS="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O3 -DNDEBUG -I/workspace/duo-tdl-examples/include/system -I/workspace/duo-tdl-examples/include/tdl"
export LDFLAGS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -L/workspace/duo-tdl-examples/libs/system/musl_riscv64 -L/workspace/duo-tdl-examples/libs/tdl/cv181x_riscv64"
export CHIP=CV181X
export COMMON_DIR=/workspace/duo-tdl-examples/common
make clean >/dev/null 2>&1
make 2>&1 | tail -20
if [ -f sample_vi_fd ]; then
    size=$(ls -lh sample_vi_fd | awk '{print $5}')
    echo ""
    echo "========== BUILD SUCCESS =========="
    echo "Binary: sample_vi_fd ($size)"
    echo "Ready to deploy to device:  scp sample_vi_fd root@192.168.42.1:/mnt/data/"
    echo "Test on device with: export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd"
    echo "                     /mnt/data/sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel"
else
    echo "BUILD FAILED: sample_vi_fd not created"
    exit 1
fi
        """
    ]
    
    print(f"[*] Using Docker image: ubuntu:22.04")
    print(f"[*] Workspace: {workspace}")
    print()
    print("[BUILD] Starting compilation...")
    print()
    
    result = subprocess.run(cmd)
    
    return result.returncode

if __name__ == "__main__":
    sys.exit(main())
