#!/usr/bin/env bash
set -euo pipefail
sshpass -p milkv ssh -o StrictHostKeyChecking=no root@192.168.42.1 "reboot" || true
sleep 25
for i in $(seq 1 30); do
  if sshpass -p milkv ssh -o StrictHostKeyChecking=no -o ConnectTimeout=3 root@192.168.42.1 "echo up"; then
    exit 0
  fi
  sleep 2
done
exit 1
