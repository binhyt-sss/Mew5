# BUILD & TEST MANUAL: sample_vi_fd with A1/A2/A3 Workarounds

## LATEST STATUS

**Device Findings:**
- Device SDK librariesare at: `/mnt/system/lib/` (not in `/root/`)
- Device does NOT have gcc/compiler installed (can only run pre-built binaries)
- **IMPLICATION**: Binary MUST be built on cross-compilation machine (Windows with RISC-V cross-compiler)

**Code Status:**
✅ **ALL THREE WORKAROUNDS IMPLEMENTED**:
- **A1**: Extended VPSS timeout from 1s → 3s (line 312-314)
- **A2**: Added `CVI_TDL_SetModelThreshold()` post-load (line 338-340)  
- **A3**: Always use explicit VPSS group `CreateHandle2()` (line 303-310)

---

## Option A: Use Pre-Built Makefile (Try First)

Since cross-compilation doesn't work reliably in this local environment, use the original Makefile approach on a proper Linux/Ubuntu system or CI environment:

```bash
# On Linux machine or CI with proper build environment
cd /path/to/duo-tdl-examples/sample_vi_fd
export CC=riscv64-unknown-linux-musl-gcc  # Or your cross-compiler
export COMMON_DIR=/path/to/duo-tdl-examples/common
export CHIP=CV181X
make clean
make
```

Then copy binary to device via SCP:
```bash
scp sample_vi_fd root@192.168.42.1:/mnt/data/
```

---

## Option B: Manual Cross-Compilation Command (Advanced)

If you have the RISC-V cross-compiler available on Windows or Linux:

**On Windows PowerShell** (with RISC-V x86_64 cross-compiler in PATH):
```powershell
$CC = "D:\Mew5\duo-tdl-examples\host-tools\gcc\riscv64-linux-musl-x86_64\bin\riscv64-unknown-linux-musl-gcc"
$INCLUDES = "-ID:\Mew5\duo-tdl-examples\include\system -ID:\Mew5\duo-tdl-examples\include\tdl"
$LDFLAGS = "-LD:\Mew5\duo-tdl-examples\libs\system\musl_riscv64 -LD:\Mew5\duo-tdl-examples\libs\tdl\cv181x_riscv64"

& $CC -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O3 `
  -DNDEBUG -DCV181X -D__CV181X__ -std=gnu11 `
  -Wno-pointer-to-int-cast -fsigned-char -fdiagnostics-color=always -s `
  $INCLUDES `
  D:\Mew5\duo-tdl-examples\sample_vi_fd\sample_vi_fd.c `
  D:\Mew5\duo-tdl-examples\common\middleware_utils.c `
  D:\Mew5\duo-tdl-examples\common\sample_utils.c `
  D:\Mew5\duo-tdl-examples\common\vi_vo_utils.c `
  -o D:\Mew5\duo-tdl-examples\sample_vi_fd\sample_vi_fd `
  -lpthread -latomic -lcvi_vb -lcvi_sys -lcvi_vi -lcvi_vpss -lcvi_venc -lcvi_bin `
  -lcvi_isp -lcvi_ae -lcvi_awb -lcvi_af -lcvi_tdl -lcvi_tdl_service -lm `
  -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 $LDFLAGS

# Result: D:\Mew5\duo-tdl-examples\sample_vi_fd\sample_vi_fd (RISC-V 64-bit ELF binary)
```

---

## Option C: Request Pre-Built Binary

Contact the development team / build server to provide a pre-compiled `sample_vi_fd` binary for CV181X RISC-V64. The binary should include:
- All three workarounds (A1/A2/A3) compiled in
- Linked against mu libc (matching device runtime)
- Target: RISC-V 64-bit (riscv64)
- Libraries: libcvi_tdl, libcvi_sys, libcvi_vpss, etc.

Request from: CI/build pipeline that has proper cross-compiler setup

---

## STEP 1: Copy Binary to Device

Once you have the compiled `sample_vi_fd` binary:

```powershell
# From Windows:
scp.exe D:\Mew5\duo-tdl-examples\sample_vi_fd\sample_vi_fd root@192.168.42.1:/mnt/data/
# Password: milkv
```

Verify copy:
```bash
# On device:
ls -lh /mnt/data/sample_vi_fd
file /mnt/data/sample_vi_fd  # Should show: ELF 64-bit LSB executable, UCB RISC-V
```

---

## STEP 2: Test the Binary on Device

On Milk-V Duo device (via SSH):

```bash
ssh root@192.168.42.1
# Password: milkv

# Setup environment
export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
export SAMPLE_TDL_DISABLE_RTSP=1

# Run test
/mnt/data/sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel 2>&1 | head -200
```

---

## EXPECTED OUTPUT

If workarounds work, you should see:

```
...[ VI/ISP/VPSS initialization output ]...
TDL setup: CreateHandle2(group=1)...                    ← A3 workaround active
TDL setup: SetVBPool(thread=0, pool=2)...               ← A3 workaround active
TDL setup: OpenModel...
anchor:-8,-8,8,8
anchor:-16,-16,16,16
anchor:-32,-32,32,32
anchor:-64,-64,64,64
anchor:-128,-128,128,128
anchor:-256,-256,256,256
... [model metadata] ...
TDL setup: Set face detection confidence threshold to 0.5    ← A2 workaround active
... [inference should continue here without crashing] ...
... [face detection output] ...
```

**Success Indicators:**
- ✅ No segmentation fault after model metadata
- ✅ "Set face detection confidence threshold" message appears  
- ✅ Inference loop runs continuously
- ✅ Face detection outputs printed

**If Crash Still Occurs:**
- Model enum/file mismatch not resolved by post-load config
- Library internal issue during tensor initialization
- → Need deeper diagnostics (strace, gdb, libcvi_tdl version check) on device

---

## FALLBACK: Diagnostic Capture on Device

If all workarounds fail, run on device to capture crash details:

```bash
# Capture system calls up to crash
export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
export SAMPLE_TDL_DISABLE_RTSP=1

# (if strace available)
strace -e trace=mmap,mprotect,brk /mnt/data/sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel 2>&1 | tail -100

# Check available memory/resources
free -h
cat /proc/meminfo | grep MemFree
ulimit -a
```

---

## CODE MODIFICATIONS SUMMARY

### File: `sample_vi_fd.c`

**Lines 303-310 (A3 - Explicit VPSS Group):**
```c
printf("TDL setup: CreateHandle2(group=1)...\n");
GOTO_IF_FAILED(CVI_TDL_CreateHandle2(&stTDLHandle, 1, 0), s32Ret, create_tdl_fail);

printf("TDL setup: SetVBPool(thread=0, pool=2)...\n");
GOTO_IF_FAILED(CVI_TDL_SetVBPool(stTDLHandle, 0, 2), s32Ret, create_service_fail);
```

**Lines 312-314 (A1 - Extended Timeout):**
```c
CVI_TDL_SetVpssTimeout(stTDLHandle, 3000);  // Was 1000ms
```

**Lines 338-340 (A2 - Post-Load Config):**
```c
CVI_TDL_SetModelThreshold(stTDLHandle, g_face_model_id, 0.5f);
printf("TDL setup: Set face detection confidence threshold to 0.5\n");
```

---

## FILES MODIFIED

- ✅ `d:\Mew5\duo-tdl-examples\sample_vi_fd\sample_vi_fd.c` — All three workarounds implemented
- ✅ `d:\Mew5\duo-tdl-examples\build_sample_vi_fd.sh` — Path corrections (unused in this approach)
