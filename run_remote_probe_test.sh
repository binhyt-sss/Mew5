#!/usr/bin/env bash
set -euo pipefail

HOST="root@192.168.42.1"
PASS="milkv"
RUN_STRACE="${SAMPLE_TDL_RUN_STRACE:-0}"
REMOTE_PROBE_PATH="${REMOTE_PROBE_PATH:-/tmp/probe_factorylike}"
MANUAL_INIT="${SAMPLE_TDL_FORCE_MANUAL_VI_INIT:-0}"
SENSOR_ONLY="${SAMPLE_VPSS_PROBE_SENSOR_ONLY:-0}"
TEST_VI_FRAMES="${SAMPLE_VPSS_PROBE_TEST_VI_FRAMES:-1}"
REMOTE_ENV="SAMPLE_TDL_FORCE_MANUAL_VI_INIT=${MANUAL_INIT} SAMPLE_VPSS_PROBE_SENSOR_ONLY=${SENSOR_ONLY} SAMPLE_VPSS_PROBE_TEST_VI_FRAMES=${TEST_VI_FRAMES}"

echo "== helper env =="
echo "REMOTE_PROBE_PATH=${REMOTE_PROBE_PATH}"
echo "MANUAL_INIT=${MANUAL_INIT} SENSOR_ONLY=${SENSOR_ONLY} TEST_VI_FRAMES=${TEST_VI_FRAMES}"

ssh_cmd() {
  sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$HOST" "$@"
}

echo "== find libini =="
ssh_cmd "find / -name 'libini.so*' 2>/dev/null | head -n 20"

echo "== list /app =="
ssh_cmd "ls -la /app 2>/dev/null | head -n 40"

echo "== cleanup old processes =="
ssh_cmd "pkill -f /tmp/probe_factorylike 2>/dev/null || true; pkill -f /tmp/probe_factorylike_new 2>/dev/null || true; pkill -f /tmp/probe_diag_01 2>/dev/null || true; pkill -f /tmp/probe_diag_02 2>/dev/null || true; pkill -f sample_vi_vpss_probe 2>/dev/null || true; pkill -f sample_vi_hand_gesture 2>/dev/null || true; sleep 1"

echo "== run probe in background =="
ssh_cmd "rm -f /tmp/probe_factorylike.out /tmp/probe_factorylike.done /tmp/probe_factorylike.exit /tmp/probe_factorylike.pid; export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/lib:/usr/lib:/app/lib:/app/libs; ${REMOTE_ENV} nohup sh -c 'timeout 15 ${REMOTE_PROBE_PATH} > /tmp/probe_factorylike.out 2>&1; echo $? > /tmp/probe_factorylike.exit; : > /tmp/probe_factorylike.done' >/tmp/probe_factorylike.nohup 2>&1 & echo \$! > /tmp/probe_factorylike.pid; cat /tmp/probe_factorylike.pid"

echo "== poll probe =="
for i in $(seq 1 20); do
  sleep 1
  if ssh_cmd "test -f /tmp/probe_factorylike.done" >/dev/null 2>&1; then
    break
  fi
done

echo "== probe highlights =="
ssh_cmd "grep -E 'VI Frame Test Scan|\[VI probe\]|GetChnFrame|SAMPLE_TDL_Init_WM failed|segmentation fault|manual VI init|factory-like VI init' /tmp/probe_factorylike.out | head -n 200 || true"
echo "== probe tail =="
ssh_cmd "tail -n 40 /tmp/probe_factorylike.out || true"
echo "== probe exit =="
ssh_cmd "cat /tmp/probe_factorylike.exit 2>/dev/null || echo running"

if [ "$RUN_STRACE" = "1" ]; then
  echo "== strace probe =="
  ssh_cmd "rm -f /tmp/trace_factorylike.log /tmp/probe_factorylike.done /tmp/probe_factorylike.exit /tmp/probe_factorylike.pid; export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd:/lib:/usr/lib:/app/lib:/app/libs; ${REMOTE_ENV} nohup sh -c 'timeout 20 strace -f -o /tmp/trace_factorylike.log ${REMOTE_PROBE_PATH} > /tmp/probe_factorylike.out 2>&1; echo $? > /tmp/probe_factorylike.exit; : > /tmp/probe_factorylike.done' >/tmp/probe_factorylike.nohup 2>&1 & echo \$! > /tmp/probe_factorylike.pid"
  for i in $(seq 1 25); do
    sleep 1
    if ssh_cmd "test -f /tmp/probe_factorylike.done" >/dev/null 2>&1; then
      break
    fi
  done
  ssh_cmd "echo 'count cvi-mipi-rx:'; grep -c 'cvi-mipi-rx' /tmp/trace_factorylike.log || true; echo 'count /dev/i2c-2:'; grep -c '/dev/i2c-2' /tmp/trace_factorylike.log || true; echo 'count 0x6d:'; grep -c '0x6d' /tmp/trace_factorylike.log || true; echo '-- hits --'; grep -E 'cvi-mipi-rx|/dev/i2c-2|0x6d' /tmp/trace_factorylike.log | head -n 60 || true"
  ssh_cmd "echo 'count any /dev/i2c-*:'; grep -Ec '/dev/i2c-[0-9]+' /tmp/trace_factorylike.log || true; echo '-- any i2c hits --'; grep -E '/dev/i2c-[0-9]+' /tmp/trace_factorylike.log | head -n 20 || true"
fi
