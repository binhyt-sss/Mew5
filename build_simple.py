#!/usr/bin/env python3
"""Simple build script for sample_vi_fd that doesn't require GNU Make."""

import os
import subprocess
import sys

# Configuration
TOOLCHAIN_PREFIX = r"D:\Mew5\duo-tdl-examples\host-tools\gcc\riscv64-linux-musl-x86_64\bin\riscv64-unknown-linux-musl-"
SOURCE_DIR = r"D:\Mew5\duo-tdl-examples"
SAMPLE_DIR = os.path.join(SOURCE_DIR, "sample_vi_fd")
COMMON_DIR = os.path.join(SOURCE_DIR, "common")
INCLUDE_SYSTEM = os.path.join(SOURCE_DIR, "include", "system")
INCLUDE_TDL = os.path.join(SOURCE_DIR, "include", "tdl")
LIB_RISCV = os.path.join(SOURCE_DIR, "libs", "system", "musl_riscv64")
LIB_TDL = os.path.join(SOURCE_DIR, "libs", "tdl", "cv181x_riscv64")

GCC = TOOLCHAIN_PREFIX + "gcc"
OUTPUT = os.path.join(SAMPLE_DIR, "sample_vi_fd")

# Compiler flags
CFLAGS = [
    "-mcpu=c906fdv",
    "-march=rv64imafdcv0p7xthead",
    "-mcmodel=medany",
    "-mabi=lp64d",
    "-O3",
    "-DNDEBUG",
    "-DCV181X",
    "-D__CV181X__",
    "-std=gnu11",
    "-Wno-pointer-to-int-cast",
    "-fsigned-char",
    "-Wno-format-truncation",
    "-fdiagnostics-color=always",
    "-s",
]

INCLUDES = [
    f"-I{INCLUDE_SYSTEM}",
    f"-I{INCLUDE_TDL}",
]

LDFLAGS = [
    "-D_LARGEFILE_SOURCE",
    "-D_LARGEFILE64_SOURCE",
    "-D_FILE_OFFSET_BITS=64",
    f"-L{LIB_RISCV}",
    f"-L{LIB_TDL}",
    "-lpthread",
    "-latomic",
    "-lcvi_vb",
    "-lcvi_sys",
    "-lcvi_vi",
    "-lcvi_vpss",
    "-lcvi_venc",
    "-lcvi_bin",
    "-lcvi_isp",
    "-lcvi_ae",
    "-lcvi_awb",
    "-lcvi_af",
    "-lcvi_tdl",
    "-lcvi_tdl_service",
    "-lm",
]

# Source files
SOURCE_FILES = [
    os.path.join(SAMPLE_DIR, "sample_vi_fd.c"),
    os.path.join(COMMON_DIR, "middleware_utils.c"),
    os.path.join(COMMON_DIR, "sample_utils.c"),
    os.path.join(COMMON_DIR, "vi_vo_utils.c"),
]

def windows_to_wsl_path(win_path):
    """Convert Windows path to WSL /mnt/host path."""
    # Convert C:\Users\... to /mnt/host/c/Users/... (lowercase drive letter)
    import re
    match = re.match(r'([A-Z]):(\\)?', win_path)
    if match:
        drive = match.group(1).lower()
        rest = win_path[2:].replace("\\", "/")
        return f"/mnt/host/{drive}{rest}"
    return "/mnt/host/" + win_path.replace("\\", "/").replace(":", "").lower()

def main():
    os.chdir(SAMPLE_DIR)
    
    # Clean first
    if os.path.exists("sample_vi_fd.o"):
        os.remove("sample_vi_fd.o")
        print("Cleaned old object files")
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
        print(f"Cleaned old binary: {OUTPUT}")
    
    # Convert paths for WSL
    gcc_wsl = windows_to_wsl_path(GCC)
    output_wsl = windows_to_wsl_path(OUTPUT)
    cflags_wsl = CFLAGS
    includes_wsl = [f"-I{windows_to_wsl_path(inc[2:])}" for inc in INCLUDES]
    sources_wsl = [windows_to_wsl_path(src) for src in SOURCE_FILES]
    ldflags_wsl = LDFLAGS
    
    # Build WSL command - use sh since bash might not be available
    cmd_parts = [gcc_wsl] + cflags_wsl + includes_wsl + sources_wsl + ["-o", output_wsl] + ldflags_wsl
    cmd_str = " ".join(f"'{p}'" if " " in p else p for p in cmd_parts)
    
    print(f"Building via WSL...")
    print(f"Command: wsl sh -c \"{cmd_str}\"")
    print()
    
    # Run compilation via WSL
    result = subprocess.run(["wsl", "sh", "-c", cmd_str])
    
    if result.returncode == 0:
        print("\nBUILD_OK")
        if os.path.exists(OUTPUT):
            print(f"Binary size: {os.path.getsize(OUTPUT)} bytes")
        return 0
    else:
        print(f"\nBUILD FAILED with code {result.returncode}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
