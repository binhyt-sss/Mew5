#!/bin/bash
sshpass -p root scp -o PasswordAuthentication=yes -o StrictHostKeyChecking=no \
  /mnt/d/Mew5/duo-tdl-examples/sample_vi_vpss_probe/sample_vi_vpss_probe \
  root@192.168.42.1:/tmp/sample_vi_vpss_probe_fixed && \
echo "Deploy probe SUCCESS"
