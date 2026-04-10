#!/usr/bin/env python3
"""
Remote build and test script for sample_vi_fd on Milk-V Duo
Uses paramiko for SSH with password authentication
"""

import paramiko
import time
import sys

HOST = "192.168.42.1"
PORT = 22
USER = "root"
PASSWORD = "milkv"

def run_remote_command(ssh, command, read_timeout=10):
    """Execute command and return output"""
    print(f">>> {command}")
    stdin, stdout, stderr = ssh.exec_command(command)
    
    # Set timeout for channel
    stdout.channel.settimeout(read_timeout)
    
    try:
        output = stdout.read().decode('utf-8', errors='ignore')
        error = stderr.read().decode('utf-8', errors='ignore')
        returncode = stdout.channel.recv_exit_status()
        
        if output:
            print(output)
        if error:
            print("[STDERR]", error, file=sys.stderr)
        
        return returncode, output, error
    except socket.timeout:
        print(f"[TIMEOUT after {read_timeout}s]")
        return -1, "", "Timeout"

def main():
    print("====== Remote Build & Test: sample_vi_fd with A1/A2/A3 ======")
    print(f"Connecting to {USER}@{HOST}...")
    
    # Connect via SSH
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    
    try:
        ssh.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=10)
        print("[OK] Connected\n")
    except Exception as e:
        print(f"[ERROR] Connection failed: {e}")
        return 1
    
    try:
        # Step 1: Create build directory
        print("[1] Creating build directory...")
        run_remote_command(ssh, "mkdir -p /mnt/build && cd /mnt/build && pwd")
        
        # Step 2: List available source files
        print("\n[2] Checking if sources need to be copied...")
        run_remote_command(ssh, "ls -lh /mnt/data/*.c 2>/dev/null | head -10")
        
        # Step 3: Get environment info
        print("\n[3] Checking device environment...")
        run_remote_command(ssh, "which gcc; gcc --version | head -3")
        print()
        run_remote_command(ssh, "ls -lh /root/duo-tdl-examples/libs/tdl/cv181x_riscv64/ | head -10")
        
        # Step 4: Show current paths for reference
        print("\n[4] Device environment:")
        run_remote_command(ssh, "echo INCLUDE_SYSTEM=/root/duo-tdl-examples/include/system; echo INCLUDE_TDL=/root/duo-tdl-examples/include/tdl; echo LIB_DIR=/root/duo-tdl-examples/libs")
        
        print("\n====== Device is ready for build ======")
        print("\nNext steps:")
        print("1. Copy source files to device:")
        print("   scp D:\\Mew5\\duo-tdl-examples\\sample_vi_fd\\sample_vi_fd.c root@192.168.42.1:/mnt/data/")
        print("   scp D:\\Mew5\\duo-tdl-examples\\common\\*.c root@192.168.42.1:/mnt/data/")
        print("\n2. SSH into device and run build (see BUILD_TEST_MANUAL.md)")
        
        ssh.close()
        return 0
        
    except Exception as e:
        print(f"[ERROR] {e}")
        ssh.close()
        return 1

if __name__ == "__main__":
    # Check if paramiko is available
    try:
        import paramiko
    except ImportError:
        print("ERROR: paramiko not installed. Install with: pip install paramiko")
        print("\nAlternatively, use the manual method:")
        print("  https://docs.microsoft.com/en-us/windows-server/administration/openssh/openssh_overview")
        sys.exit(1)
    
    sys.exit(main())
