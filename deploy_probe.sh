#!/bin/bash
set -e
FILE="/mnt/d/Mew5/duo-tdl-examples/sample_vi_vpss_probe/sample_vi_vpss_probe"
if [ ! -f "$FILE" ]; then
  echo "Error: $FILE not found"
  exit 1
fi

echo "Deploying $FILE to board..."
# Try to deploy with ssh key auth
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@192.168.42.1 "rm -f /tmp/sample_vi_vpss_probe" 2>&1 || echo "Pre-clean completed..."
scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "$FILE" root@192.168.42.1:/tmp/ && echo "Deploy OK" || (echo "Deploy FAILED"; exit 1)
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@192.168.42.1 "chmod +x /tmp/sample_vi_vpss_probe && ls -lh /tmp/sample_vi_vpss_probe"
