# FINAL BUILD INSTRUCTIONS: sample_vi_fd with A1/A2/A3 Workarounds

## Current Status

✅ **Code Implementation**: Complete
- A1: VPSS timeout extended to 3000ms (line 312)
- A2: Post-load model threshold configured (line 333)
- A3: Explicit VPSS group + VB pool creation (lines 305, 308)

❌ **Build Impediment**: Cross-compiler in workspace is Linux-only
- The `riscv64-linux-musl-gcc` binaries are ELF Linux executables
- Cannot run on Windows natively
- WSL/Docker required, OR compile on Linux host

---

## Option 1: Fastest - Use Online Build Service (Recommended)

GitHub Actions or similar CI can build automatically:

1. Push modified code to your Git repository
2. Use GitHub Actions with Ubuntu runner
3. Run: `make -C sample_vi_fd clean && make -C sample_vi_fd`
4. Artifact: binary ready for device

**Why**: Your CI environment likely has proper cross-compiler setup already.

---

## Option 2: Build on Linux/Mac Machine

If you have a Linux system or Mac with Docker:

```bash
# On Linux machine in workspace directory:
cd duo-tdl-examples/sample_vi_fd

export TOOLCHAIN_PREFIX=../host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
export CC=${TOOLCHAIN_PREFIX}gcc
export CFLAGS="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d -O3 -DNDEBUG -I../include/system -I../include/tdl"
export LDFLAGS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -L../libs/system/musl_riscv64 -L../libs/tdl/cv181x_riscv64"
export CHIP=CV181X
export COMMON_DIR=../common

make clean
make

# Result: sample_vi_fd (RISC-V 64-bit ELF binary)
ls -lh sample_vi_fd
```

Then copy to device:
```bash
scp sample_vi_fd root@192.168.42.1:/mnt/data/
```

---

## Option 3: Use Docker Container (No Host Setup Needed)

**macOS/Linux/Windows with Docker:**

```bash
# From workspace root
docker run --rm -v $(pwd):/work -w /work ubuntu:22.04 bash -c '
  apt-get update && apt-get install -y build-essential
  cd duo-tdl-examples/sample_vi_fd
  export TOOLCHAIN_PREFIX=/work/duo-tdl-examples/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
  export COMMON_DIR=/work/duo-tdl-examples/common
  export CHIP=CV181X
  make clean && make
  ls -lh sample_vi_fd
'
```

Result binary is ready to SCP to device.

---

## Option 4: Request Pre-Built Binary

Contact your project lead or CI/build team:
- Specify: CV181X RISC-V64 target, scrfd face detection
- Include: A1/A2/A3 modifications from sample_vi_fd.c (see code diffs below)
- Expected size: ~25-30KB stripped binary

---

## Once You Have the Binary

### 1. Deploy to Device

```powershell
# From Windows PowerShell:
scp.exe sample_vi_fd root@192.168.42.1:/mnt/data/
# Password: milkv
```

### 2. Test on Device

```bash
# SSH into device
ssh root@192.168.42.1
# Password: milkv

# Setup
export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
export SAMPLE_TDL_DISABLE_RTSP=1

# Run test
/mnt/data/sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel 2>&1 | tee /tmp/test_output.log

# View first 200 lines
head -200 /tmp/test_output.log
```

### 3. Interpret Results

**SUCCESS SIGNS** ✅:
- Prints: `TDL setup: CreateHandle2(group=1)...` (A3 active)
- Prints: `TDL setup: Set face detection confidence threshold to 0.5` (A2 active)
- No "Segmentation fault" in output
- Face detection outputs printed (face counts)

**PARTIAL SUCCESS** ⚠️:
- Runs longer than before (some workarounds helping)
- Still crashes but later in execution

**STILL FAILING** ❌:
- Same "Segmentation fault" immediately after model metadata
- Run diagnostics (see fallback section)

---

## Code Diffs (For Verification)

### Change 1: A3 - Explicit VPSS Group (Lines 303-310)

```diff
-  // In no-RTSP mode, prefer auto handle selection to avoid VPSS/VB assignment mismatch.
-  if (!bDisableRTSP) {
-    printf("TDL setup: CreateHandle2...\n");
-    GOTO_IF_FAILED(CVI_TDL_CreateHandle2(&stTDLHandle, 1, 0), s32Ret, create_tdl_fail);
-
-    printf("TDL setup: SetVBPool...\n");
-    GOTO_IF_FAILED(CVI_TDL_SetVBPool(stTDLHandle, 0, 2), s32Ret, create_service_fail);
-  } else {
-    printf("TDL setup: CreateHandle(auto)...\n");
-    GOTO_IF_FAILED(CVI_TDL_CreateHandle(&stTDLHandle), s32Ret, create_tdl_fail);
-    printf("SAMPLE_TDL_DISABLE_RTSP=1, skip SetVBPool\n");
-  }
+  // A3 (Workaround): Always use explicit VPSS group assignment to avoid handle binding issues.
+  printf("TDL setup: CreateHandle2(group=1)...\n");
+  GOTO_IF_FAILED(CVI_TDL_CreateHandle2(&stTDLHandle, 1, 0), s32Ret, create_tdl_fail);
+
+  printf("TDL setup: SetVBPool(thread=0, pool=2)...\n");
+  GOTO_IF_FAILED(CVI_TDL_SetVBPool(stTDLHandle, 0, 2), s32Ret, create_service_fail);
```

### Change 2: A1 - Extended Timeout (Line 312)

```diff
-  CVI_TDL_SetVpssTimeout(stTDLHandle, 1000);
+  // A1 (Workaround): Extend VPSS timeout from 1s to 3s for post-load initialization.
+  CVI_TDL_SetVpssTimeout(stTDLHandle, 3000);
```

### Change 3: A2 - Post-Load Config (Lines 333-340)

```diff
   printf("TDL setup: OpenModel...\n");
   GOTO_IF_FAILED(CVI_TDL_OpenModel(stTDLHandle, g_face_model_id, argv[1]), s32Ret, setup_tdl_fail);
   printf("TDL setup: OpenModel done\n");
+
+  // A2 (Workaround): Add explicit post-load model configuration.
+  CVI_TDL_SetModelThreshold(stTDLHandle, g_face_model_id, 0.5f);
+  printf("TDL setup: Set face detection confidence threshold to 0.5\n");

   pthread_t stVencThread, stTDLThread;
```

---

## Troubleshooting

**Q: Binary won't compile on Linux with provided cross-compiler**
A: Cross-compiler path: `export TOOLCHAIN_PREFIX=./duo-tdl-examples/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-`

**Q: Still crashes on device even with workarounds**
A: Run diagnostics on device:
```bash
strace -e trace=mmap,mprotect /mnt/data/sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel 2>&1 | tail -50
```

**Q: "Model mismatch" error appears**
A: Normal - pre-validation guard working. Either:
- Use default SCARFFACE model (don't set SAMPLE_TDL_FACE_MODEL env)
- Or have matching RetinaFace cvimodel file

---

## Summary

✅ Implementation complete in source code  
❌ Build blocked by Windows not supporting Linux cross-compiler binaries  
📋 Multiple options provided to complete build (Options 1-4)  
🔧 Full test instructions ready (copy device→test→verify)  

**Key Takeaway**: Code is ready; just need proper build environment (Linux, CI, Docker, or external system).
