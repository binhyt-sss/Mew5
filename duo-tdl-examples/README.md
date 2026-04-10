
# Milk-V Duo series TDL-SDK examples
English | [简体中文](./README-zh.md)

## Introduction to TDL-SDK

Cvitek provides TDL integration algorithms to reduce the time required for application development. This architecture realizes the algorithm required by TDL, including its pre and post processing, and provides a unified and convenient programming interface.

Reference Links: [https://github.com/sophgo/tdl_sdk](https://github.com/sophgo/tdl_sdk)

## Introduction to this repository

This repository is based on [tdl_sdk](https://github.com/sophgo/tdl_sdk) and is created for the Milk-V Duo series development board, so that users can use each example separately for verification testing or develop related applications.

**The examples in this repository need to be tested in the V2 version of the firmware, image address:** [Relese](https://github.com/milkv-duo/duo-buildroot-sdk-v2/releases)。

## Development Environment

Use a Ubuntu system, `Ubuntu 22.04 LTS` is recommended.

You can also use an Ubuntu system in a virtual machine, Ubuntu installed in WSL in Windows, or an Ubuntu system based on Docker.

Install the compilation dependent tools:
```
sudo apt-get install wget git make
```

Get the repository code:
```
git clone https://github.com/milkv-duo/duo-tdl-examples.git
```

## Compile Example

The following takes the compilation of face detection program as an example to introduce the compilation process.

Enter the code directory:
```
cd duo-tdl-examples
```

Prepare the compilation environment:
```
source envsetup.sh
```
The first time you source it, the required toolchain will be automatically downloaded. The downloaded directory is named `host-tools`. When source it next time, if the directory already exists, it will not be downloaded again.

In the source process, you need to enter the required compilation target as prompted:
```
Select Product:
1. Duo (CV1800B)
2. Duo256M (SG2002) or DuoS (SG2000)
```
If the target board is Duo, select `1`, if the target board is Duo256M or DuoS, select `2`. Since Duo256M and DuoS support both RISCV and ARM architectures, you need to continue to select as prompted:
```
Select Arch:
1. ARM64
2. RISCV64
Which would you like:
```
If the test program needs to be run on a ARM system, select `1`, if it is an RISCV system, select `2`.

**In the same terminal, you only need to source it once.**

The sample program for face detection is `sample_vi_fd`. Enter the sample_vi_fd directory:
```
cd sample_vi_fd
```
Compile using the `make` command:
```
make
```
Send the `sample_vi_fd` program generated in the current directory to the Duo series board via `scp` or other methods for testing. If you need to clear the generated program, you can execute `make clean` to clear it.

## Quick Build/Run Guide (Windows + WSL Ubuntu-22.04, Duo256M RISC-V)

This section provides a verified workflow for this repository on Windows hosts using WSL `Ubuntu-22.04`.

1. Confirm WSL distro:
   ```bash
   wsl -l
   ```
2. Build with the correct distro and shell (`bash`):
   ```bash
   wsl -d Ubuntu-22.04 bash -lc 'cd /mnt/d/Mew5/duo-tdl-examples && bash ./build_ubuntu2204_hand.sh'
   ```
3. Deploy binary to board:
   ```bash
   wsl -d Ubuntu-22.04 sshpass -p milkv scp -o StrictHostKeyChecking=no \
     /mnt/d/Mew5/duo-tdl-examples/sample_vi_hand_gesture/sample_vi_hand_gesture \
     root@192.168.42.1:/tmp/sample_vi_hand_gesture_trace
   wsl -d Ubuntu-22.04 sshpass -p milkv ssh -o StrictHostKeyChecking=no \
     root@192.168.42.1 'chmod +x /tmp/sample_vi_hand_gesture_trace'
   ```
4. Run on board:
   ```bash
   wsl -d Ubuntu-22.04 sshpass -p milkv ssh -o StrictHostKeyChecking=no root@192.168.42.1 '
     export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
     /tmp/sample_vi_hand_gesture_trace \
       /mnt/cvimodel/yolov8n_det_hand_384_640_INT8_cv181x.cvimodel \
       /mnt/cvimodel/keypoint_hand_128_128_INT8_cv181x.cvimodel \
       /mnt/cvimodel/cls_keypoint_hand_gesture_1_42_INT8_cv181x.cvimodel
   '
   ```

### Optional debug flags

- `SAMPLE_TDL_VI_TRACE=1`: print VI init trace and timing.
- `SAMPLE_TDL_FORCE_MANUAL_VI_INIT=1`: bypass `SAMPLE_PLAT_VI_INIT` and execute VI init step-by-step (`StartSensor`, `StartDev`, `StartMIPI`, `SensorProbe`, `CreateIsp`, `StartViChn`).
- `SAMPLE_TDL_VI_STARTUP_DELAY_MS=500`: delay between `CreateIsp` and `StartViChn` in manual VI init path.
- `SAMPLE_TDL_VPSS_PROBE_DELAY_MS=800`: delay before the first VPSS channel probe in hand gesture sample.
- `SAMPLE_TDL_VPSS_PROBE_TIMEOUT_MS=1500`: per-attempt timeout for VPSS probe.
- `SAMPLE_TDL_VPSS_PROBE_RETRIES=6`: number of VPSS probe retries.
- `SAMPLE_TDL_VPSS_INPUT_PROFILE=mem_isp|dual_isp|dual_mem`: VPSS dual-mode input mapping used by hand gesture sample.
- `SAMPLE_TDL_VPSS_ATTACH_VB=0|1`: disable or enable VB pool attach to VPSS channels (default `1`).
- `SAMPLE_TDL_VI_BIND_CHN=0|1`: select VI channel for VI→VPSS bind (default `0`, `1` may fail on some boards).
- `SAMPLE_TDL_MANUAL_CREATE_PIPE=0|1`: opt-in manual `CVI_VI_CreatePipe/StartPipe` in manual VI init path (default `0`).
- `SAMPLE_VPSS_PROBE_TEST_VI_FRAMES=0|1`: enable direct VI frame scan in the probe sample (default `1`).
- `SAMPLE_VI_PROBE_SCAN_PIPES=N`: number of VI pipes to scan when direct VI probing is enabled (default `2`).
- `SAMPLE_VI_PROBE_SCAN_CHNS=N`: number of VI channels to scan per pipe (default `4`).

Example:
```bash
wsl -d Ubuntu-22.04 sshpass -p milkv ssh -o StrictHostKeyChecking=no root@192.168.42.1 '
  export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
  export SAMPLE_TDL_VI_TRACE=1
  export SAMPLE_TDL_FORCE_MANUAL_VI_INIT=1
  /tmp/sample_vi_hand_gesture_trace \
    /mnt/cvimodel/yolov8n_det_hand_384_640_INT8_cv181x.cvimodel \
    /mnt/cvimodel/keypoint_hand_128_128_INT8_cv181x.cvimodel \
    /mnt/cvimodel/cls_keypoint_hand_gesture_1_42_INT8_cv181x.cvimodel
'
```

### Known pitfalls

- Running `wsl` without `-d Ubuntu-22.04` may enter another distro (for example `docker-desktop`) and break build tools.
- Running script via `sh` can fail on `set -o pipefail`; use `bash`.
- If `scp` reports `Text file busy`, copy to a new filename on board (for example `/tmp/sample_vi_hand_gesture_trace`).
- If `CVI_VPSS_CreateGrp ... 0xc0068004` appears, there may be stale VPSS state from a previous crash; reboot board and retry.

### Current investigation status (2026-04-09)

- Build path and deployment workflow above are verified working on Windows + WSL Ubuntu-22.04.
- In current investigation branch, middleware includes:
  - explicit `CVI_VI_EnableChn` after VI init,
  - optional manual VI init (`SAMPLE_TDL_FORCE_MANUAL_VI_INIT=1`),
  - one-time VPSS init retry after `StopGrp/DestroyGrp`.
- The probe now also supports a short sensor-only diagnostic mode via `--manual --sensor-only`, which stops after the direct VI scan and avoids the long VPSS loop during sensor bring-up debugging.
- The latest trace shows that `/dev/i2c-2` now opens successfully in the manual path, but `SAMPLE_COMM_ISP_PatchSnsObj` and `SAMPLE_COMM_ISP_Sensor_Regiter_callback` still fail and `pfnSnsProbe` continues to return `0xc00e8006`; the remaining issue is now the ISP-side sensor registration state, not raw MIPI or I2C device access.
- The latest manual-path update now reaches `SAMPLE_COMM_VI_SensorProbe ret=0x0` with confirmed `/dev/cvi-mipi-rx` and `/dev/i2c-2` activity, but direct `CVI_VI_GetChnFrame` still returns `0xc00e8040`; current blocker has shifted to VI frame dequeue/runtime path rather than sensor detection.
- On the latest board reruns after reboot:
  - the default factory-like path reaches VI/VPSS/VENC/RTSP startup but still returns `CVI_VPSS_GetChnFrame ... 0xc006800e`.
  - the manual path now opens `/dev/cvi-mipi-rx` and issues MIPI ioctls, but `pfnSnsProbe` still returns `0xc00e8006` and frames do not flow.

This means build/deploy is healthy, but the remaining issue is in VI/VPSS runtime behavior on board.

### Probe command used for current status check

```bash
wsl -d Ubuntu-22.04 bash -lc '
  cd /mnt/d/Mew5/duo-tdl-examples
  bash build_ubuntu2204_vpss_probe.sh
  sshpass -p milkv scp -o StrictHostKeyChecking=no \
    sample_vi_vpss_probe/sample_vi_vpss_probe \
    root@192.168.42.1:/tmp/sample_vi_vpss_probe_fix
  sshpass -p milkv ssh -o StrictHostKeyChecking=no root@192.168.42.1 "
    chmod +x /tmp/sample_vi_vpss_probe_fix
    export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
    SAMPLE_TDL_FORCE_MANUAL_VI_INIT=1 \
    SAMPLE_TDL_DISABLE_VENC=1 \
    SAMPLE_VPSS_PROBE_TEST_VI_FRAMES=1 \
    /tmp/sample_vi_vpss_probe_fix
  "
'
```

Programs directly transferred to the Duo development board through `scp` may not have execution permissions. You need to add executable permissions in the development board's system using the `chmod` command:
```
chmod +x sample_vi_fd
```

The command to test the face detection example in the development board is `sample_vi_fd + face detection model file`. **Note that the models used in Duo and Duo256M/DuoS are different**:

- Duo (CV180X)
  ```
  ./sample_vi_fd /mnt/cvimodel/scrfd_320_256_ir_0x.cvimodel
  ```

- Duo256M/DuoS (SG200X)
  ```
  ./sample_vi_fd /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel
  ```

At this time, point the camera at the face, and the terminal log will print the number of faces currently detected. If you need to preview the video screen in real time on a computer, please refer to [Face Detection Documentation](https://milkv.io/docs/duo/application-development/tdl-sdk/tdl-sdk-face-detection).


## Examples Documentation

For more examples and detailed instructions, please refer to the [Milk-V Documentation](https://milkv.io/docs/duo/application-development/tdl-sdk/tdl-sdk-introduction).

## Model support list

### TDL-SDK Model Data Types

[https://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/TDL_SDK_Software_Development_Guide/build/html/6_Data_Types.html](https://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/TDL_SDK_Software_Development_Guide/build/html/6_Data_Types.html)

### TDK-SDK Model Download

[https://github.com/sophgo/tdl_models/tree/main](https://github.com/sophgo/tdl_models/tree/main)

## Reference Documentation

<table>
<thead>
  <tr>
    <td>Chinese Version(中文版)</td>
    <td colspan="2">格式</td>
    <td>English Version</td>
    <td colspan="2">Format</td>
  </tr>
</thead>
<tbody>
  <tr>
    <td>深度学习SDK软件开发指南</td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/TDL_SDK_Software_Development_Guide/build/html/index.html">html</a></td>
    <td><a href="https://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/TDL_SDK_Software_Development_Guide/build/TDLSDKSoftwareDevelopmentGuide_zh.pdf">pdf</a></td>
    <td>TDL SDK Software Development Guide</td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/TDL_SDK_Software_Development_Guide/build/html/index.html">html</a></td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/TDL_SDK_Software_Development_Guide/build/TDLSDKSoftwareDevelopmentGuide_en.pdf">pdf</a></td>
  </tr>
  <tr>
    <td>YOLO系列开发指南</td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/YOLO_Development_Guide/build/html/index.html">html</a></td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/zh/01.software/TPU/YOLO_Development_Guide/build/YOLODevelopmentGuide_zh.pdf">pdf</a></td>
    <td>YOLO Development Guide</td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/YOLO_Development_Guide/build/html/index.html">html</a></td>
    <td><a href="http://doc.sophgo.com/cvitek-develop-docs/master/docs_latest_release/CV180x_CV181x/en/01.software/TPU/YOLO_Development_Guide/build/YOLODevelopmentGuide_en.pdf">pdf</a></td>
  </tr>
</tbody>
</table>

## FAQs

### What is the difference between this repository and the Sophgo TDL SDK repository?

The compilation of the original [tdl_sdk](https://github.com/sophgo/tdl_sdk) repository depends on the buildroot SDK [duo-buildroot-sdk-v2](https://github.com/milkv-duo/duo-buildroot-sdk-v2), and the compilation process is slightly more complicated. This repository separates the sample code from tdl_sdk, so users who only care about AI program development can compile and test directly. The source code of the examples in this repository is consistent with tdl_sdk, and the updates of the relevant code in tdl_sdk will be synchronized regularly.

### Are the dynamic library so files in the libs directory open source?

To simplify the compilation of the sample programs, the libs directory contains the precompiled dynamic library so files that the samples depend on. Most of the source code of these libraries can be found in [tdl_sdk](https://github.com/sophgo/tdl_sdk) and [cvi_mpi](https://github.com/sophgo/cvi_mpi). For the source code files corresponding to each library, please refer to the README file in the libs directory of this repository.
