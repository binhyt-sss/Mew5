#!/usr/bin/env bash
set -euo pipefail

sshpass -p milkv ssh -o StrictHostKeyChecking=no root@192.168.42.1 '
export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd
rm -f /tmp/trace_manual_vi.log /tmp/probe_manual_vi.out

(timeout 20 strace -f -o /tmp/trace_manual_vi.log \
  env SAMPLE_TDL_FORCE_MANUAL_VI_INIT=1 \
      SAMPLE_TDL_MANUAL_CREATE_PIPE=1 \
      SAMPLE_TDL_DISABLE_VENC=1 \
      SAMPLE_VPSS_PROBE_TIMEOUT_MS=700 \
      SAMPLE_VPSS_PROBE_ROUNDS=1 \
      SAMPLE_VPSS_PROBE_SLEEP_MS=20 \
      SAMPLE_VPSS_PROBE_TEST_VI_FRAMES=0 \
      /tmp/sample_vi_vpss_probe_fix > /tmp/probe_manual_vi.out 2>&1) || true

echo "===== PROBE TAIL ====="
tail -40 /tmp/probe_manual_vi.out || true

echo "===== MIPI/I2C OPEN ====="
grep -E "openat\(.*(/dev/cvi-mipi-rx|/dev/i2c-2)" /tmp/trace_manual_vi.log | head -20 || true

echo "===== MIPI IOCTL (0x6d) ====="
grep -E "ioctl\(.*0x6d" /tmp/trace_manual_vi.log | head -20 || true
'
