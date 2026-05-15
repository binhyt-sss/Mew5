"""
PlatformIO extra script: auto-pack + flash srmodels.bin after firmware upload.
ESP-SR's movemodel.py reads sdkconfig.<env> (not a plain 'sdkconfig' file),
so we call it manually with the correct path before flashing.
"""
import os
import subprocess
import sys

Import("env")

def flash_srmodels(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    build_dir   = env.subst("$BUILD_DIR")
    pioenv      = env.subst("$PIOENV")

    # ── 1. Pack models ───────────────────────────────────────────────────────
    sdkconfig   = os.path.join(project_dir, f"sdkconfig.{pioenv}")
    component   = os.path.join(project_dir, "managed_components", "espressif__esp-sr")
    mvmodel_py  = os.path.join(component, "model", "movemodel.py")

    if not os.path.isfile(sdkconfig):
        print(f"[flash_model] WARNING: sdkconfig not found at {sdkconfig}")
    else:
        print("[flash_model] Packing speech models (vadnet + wakenet)...")
        ret = subprocess.run([sys.executable, mvmodel_py,
                              "-d1", sdkconfig,
                              "-d2", component,
                              "-d3", build_dir])
        if ret.returncode != 0:
            print("[flash_model] ERROR: movemodel.py failed")
            return

    # ── 2. Find srmodels.bin ─────────────────────────────────────────────────
    srmodels_bin = os.path.join(build_dir, "srmodels", "srmodels.bin")
    if not os.path.isfile(srmodels_bin):
        print(f"[flash_model] ERROR: srmodels.bin not found at {srmodels_bin}")
        return

    # ── 3. Read partition offset from partitions.csv ─────────────────────────
    partitions_csv = os.path.join(project_dir, "partitions.csv")
    model_offset = None
    with open(partitions_csv) as f:
        for line in f:
            line = line.strip()
            if line.startswith("#") or not line:
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) >= 4 and parts[0] == "model":
                model_offset = parts[3]
                break

    if model_offset is None:
        print("[flash_model] ERROR: 'model' partition not found in partitions.csv")
        return

    # ── 4. Flash ─────────────────────────────────────────────────────────────
    upload_port  = env.subst("$UPLOAD_PORT")
    upload_speed = env.subst("$UPLOAD_SPEED")

    cmd = [sys.executable, "-m", "esptool",
           "--chip", "esp32s3",
           "--port", upload_port,
           "--baud", upload_speed,
           "write-flash",
           model_offset, srmodels_bin]

    print(f"[flash_model] Flashing srmodels.bin → offset {model_offset} ...")
    ret = subprocess.run(cmd)
    if ret.returncode != 0:
        print("[flash_model] ERROR: Failed to flash srmodels.bin")
    else:
        print("[flash_model] srmodels.bin flashed successfully!")

env.AddPostAction("upload", flash_srmodels)
