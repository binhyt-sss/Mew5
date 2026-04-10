@echo off
REM Standalone build script for sample_vi_fd with A1/A2/A3 workarounds
REM This script compiles using the RISC-V cross-compiler included in the workspace
REM Target: Milk-V Duo (CV181X RISC-V64)

setlocal enabledelayedexpansion

echo ============================================================
echo Build sample_vi_fd - TDL Post-Load Crash Workarounds A1/A2/A3
echo ============================================================
echo.

set "WORKSPACE=D:\Mew5"
set "PROJECT=%WORKSPACE%\duo-tdl-examples"
set "SAMPLE_DIR=%PROJECT%\sample_vi_fd"
set "COMMON_DIR=%PROJECT%\common"
set "TOOLCHAIN=%PROJECT%\host-tools\gcc\riscv64-linux-musl-x86_64\bin"
set "GCC=%TOOLCHAIN%\riscv64-unknown-linux-musl-gcc.exe"
set "OUTPUT=%SAMPLE_DIR%\sample_vi_fd"

echo [1] Verifying cross-compiler...
if not exist "%GCC%" (
  echo ERROR: Cross-compiler not found at: %GCC%
  exit /b 1
)
echo OK: %GCC%
echo.

echo [2] Verifying source files...
for %%F in (
  "%SAMPLE_DIR%\sample_vi_fd.c"
  "%COMMON_DIR%\middleware_utils.c"
  "%COMMON_DIR%\sample_utils.c"
  "%COMMON_DIR%\vi_vo_utils.c"
) do (
  if not exist "%%F" (
    echo ERROR: Missing source file: %%F
    exit /b 1
  )
)
echo OK: All source files present
echo.

echo [3] Building binary...
echo Output: %OUTPUT%
echo.

REM Build command with all flags
"%GCC%" ^
  -mcpu=c906fdv ^
  -march=rv64imafdcv0p7xthead ^
  -mcmodel=medany ^
  -mabi=lp64d ^
  -O3 ^
  -DNDEBUG ^
  -DCV181X ^
  -D__CV181X__ ^
  -std=gnu11 ^
  -Wno-pointer-to-int-cast ^
  -fsigned-char ^
  -Wno-format-truncation ^
  -fdiagnostics-color=always ^
  -s ^
  -I"%PROJECT%\include\system" ^
  -I"%PROJECT%\include\tdl" ^
  "%SAMPLE_DIR%\sample_vi_fd.c" ^
  "%COMMON_DIR%\middleware_utils.c" ^
  "%COMMON_DIR%\sample_utils.c" ^
  "%COMMON_DIR%\vi_vo_utils.c" ^
  -o "%OUTPUT%" ^
  -D_LARGEFILE_SOURCE ^
  -D_LARGEFILE64_SOURCE ^
  -D_FILE_OFFSET_BITS=64 ^
  -L"%PROJECT%\libs\system\musl_riscv64" ^
  -L"%PROJECT%\libs\tdl\cv181x_riscv64" ^
  -lpthread ^
  -latomic ^
  -lcvi_vb ^
  -lcvi_sys ^
  -lcvi_vi ^
  -lcvi_vpss ^
  -lcvi_venc ^
  -lcvi_bin ^
  -lcvi_isp ^
  -lcvi_ae ^
  -lcvi_awb ^
  -lcvi_af ^
  -lcvi_tdl ^
  -lcvi_tdl_service ^
  -lm

if errorlevel 1 (
  echo.
  echo ERROR: Compilation failed with code !errorlevel!
  exit /b 1
)

echo.
echo [4] Verifying output...
if not exist "%OUTPUT%" (
  echo ERROR: Output binary not created
  exit /b 1
)

for /F "usebackq" %%A in ('%OUTPUT%') do (
  set "SIZE=%%~zA"
)

echo OK: Binary created
echo Size: %SIZE% bytes
echo Location: %OUTPUT%
echo.

echo ============================================================
echo BUILD SUCCESSFUL
echo ============================================================
echo.
echo Next steps:
echo 1. Copy to device:
echo    scp.exe "%OUTPUT%" root@192.168.42.1:/mnt/data/
echo.
echo 2. Test on device:
echo    ssh root@192.168.42.1
echo    export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
echo    export SAMPLE_TDL_DISABLE_RTSP=1
echo    /mnt/data/sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel
echo.

exit /b 0
