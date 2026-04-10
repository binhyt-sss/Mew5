#!/usr/bin/env python3
"""
Copy sources to device and build sample_vi_fd with A1/A2/A3 workarounds
"""

import paramiko
import time
import sys
import os

HOST = "192.168.42.1"
PORT = 22
USER = "root"
PASSWORD = "milkv"

def run_remote_command(ssh, command, read_timeout=15):
    """Execute command and return output"""
    print(f">>> {command}")
    stdin, stdout, stderr = ssh.exec_command(command)
    stdout.channel.settimeout(read_timeout)
    
    try:
        output = stdout.read().decode('utf-8', errors='ignore')
        error = stderr.read().decode('utf-8', errors='ignore')
        returncode = stdout.channel.recv_exit_status()
        
        if output:
            for line in output.strip().split('\n'):
                print(f"    {line}")
        if error and returncode != 0:
            print(f"   [ERR] {error[:200]}", file=sys.stderr)
        
        return returncode, output, error
    except Exception as e:
        print(f"   [TIMEOUT/ERROR: {e}]")
        return -1, "", str(e)

def copy_file(sftp, local_path, remote_path):
    """Copy file via SFTP"""
    try:
        print(f"  Copying {os.path.basename(local_path)} -> {remote_path}")
        sftp.put(local_path, remote_path)
        print(f"    [OK]")
        return 0
    except Exception as e:
        print(f"    [ERROR] {e}")
        return 1

def main():
    print("====== Copy Sources & Build sample_vi_fd ======\n")
    
    # Connect via SSH
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    
    try:
        print(f"[*] Connecting to {USER}@{HOST}...")
        ssh.connect(HOST, port=PORT, username=USER, password=PASSWORD, timeout=10)
        print("    [OK]\n")
    except Exception as e:
        print(f"    [ERROR] {e}")
        return 1
    
    # Open SFTP
    try:
        sftp = ssh.open_sftp()
    except Exception as e:
        print(f"[ERROR] SFTP failed: {e}")
        ssh.close()
        return 1
    
    try:
        # Step 1: Find SDK location
        print("[1] Finding SDK paths on device...")
        rc, out, _ = run_remote_command(ssh, "find /mnt -name 'libcvi_tdl.so' 2>/dev/null | head -3")
        if rc == 0 and out.strip():
            sdk_lib_path = os.path.dirname(out.strip().split('\n')[0])
            sdk_root = os.path.dirname(os.path.dirname(sdk_lib_path))
            print(f"    Found SDK at: {sdk_root}\n")
        else:
            print("    [INFO] SDK libraries not found in /mnt, checking /root...")
            sdk_root = "/root/duo-tdl-examples"
            run_remote_command(ssh, f"ls -lh {sdk_root}/libs/tdl/ 2>/dev/null | head -5")
        
        # Step 2: Create build directory
        print("\n[2] Setting up build directory...")
        run_remote_command(ssh, "mkdir -p /mnt/build_tdl && cd /mnt/build_tdl && pwd")
        
        # Step 3: Copy source files
        print("\n[3] Copying source files to device...")
        sources = [
            ("D:\\Mew5\\duo-tdl-examples\\sample_vi_fd\\sample_vi_fd.c", "/mnt/build_tdl/sample_vi_fd.c"),
            ("D:\\Mew5\\duo-tdl-examples\\common\\middleware_utils.c", "/mnt/build_tdl/middleware_utils.c"),
            ("D:\\Mew5\\duo-tdl-examples\\common\\sample_utils.c", "/mnt/build_tdl/sample_utils.c"),
            ("D:\\Mew5\\duo-tdl-examples\\common\\vi_vo_utils.c", "/mnt/build_tdl/vi_vo_utils.c"),
        ]
        
        for local, remote in sources:
            if os.path.exists(local):
                copy_file(sftp, local, remote)
            else:
                print(f"  [SKIP] {local} not found")
        
        # Step 4: Check files on device
        print("\n[4] Verifying files on device...")
        run_remote_command(ssh, "ls -lh /mnt/build_tdl/")
        
        # Step 5: Find SDK paths on device for compilation
        print("\n[5] Discovering SDK paths on device...")
        rc, out, _ = run_remote_command(ssh, 
            "echo 'Checking /mnt/shared...'; " +
            "ls -lh /mnt/shared/duo-tdl-examples/include/system/ 2>/dev/null | head -3 || " +
            "echo 'Not in /mnt/shared'; " +
            "echo 'Checking /root...'; " +
            "ls -lh /root/duo-tdl-examples/include/system/ 2>/dev/null | head -3 || " +
            "echo 'Not in /root'")
        
        # Step 6: Attempt compile
        print("\n[6] Attempting to compile on device...")
        
        # Try to find gcc first
        rc, out, _ = run_remote_command(ssh, "cc --version 2>&1 | head -1 || gcc --version 2>&1 | head -1 || echo 'No compiler found'")
        
        if "not found" not in out.lower() and rc == 0:
            print("\n    [OK] Compiler available, building...")
            
            # Build command
            build_cmd = (
                "cd /mnt/build_tdl && "
                "cc -c -O2 -DNDEBUG -DCV181X -D__CV181X__ "
                "-std=gnu11 -Wno-pointer-to-int-cast -fsigned-char "
                "-I/root/duo-tdl-examples/include/system -I/root/duo-tdl-examples/include/tdl "
                "sample_vi_fd.c middleware_utils.c sample_utils.c vi_vo_utils.c && "
                "cc *.o -o sample_vi_fd_workaround "
                "-L/root/duo-tdl-examples/libs/tdl/cv181x_riscv64 "
                "-L/root/duo-tdl-examples/libs/system/musl_riscv64 "
                "-lpthread -latomic -lcvi_vb -lcvi_sys -lcvi_vi -lcvi_vpss -lcvi_venc "
                "-lcvi_bin -lcvi_isp -lcvi_ae -lcvi_awb -lcvi_af -lcvi_tdl -lcvi_tdl_service -lm && "
                "ls -lh sample_vi_fd_workaround"
            )
            rc, out, err = run_remote_command(ssh, build_cmd, read_timeout=60)
            
            if rc == 0 and "sample_vi_fd_workaround" in out:
                print("\n    [SUCCESS] Build completed!")
                
                # Step 7: Test the binary
                print("\n[7] Testing new binary...")
                test_cmd = (
                    "export LD_LIBRARY_PATH=/mnt/system/lib:/mnt/system/usr/lib:/mnt/system/usr/lib/3rd && "
                    "export SAMPLE_TDL_DISABLE_RTSP=1 && "
                    "/mnt/build_tdl/sample_vi_fd_workaround /mnt/cvimodel/scrfd_768_432_int8_1x.cvimodel 2>&1"
                )
                rc, out, _ = run_remote_command(ssh, test_cmd, read_timeout=30)
                
                # Display first 150 lines of output
                lines = out.split('\n')[:150]
                print("\n" + "="*60)
                print("TEST OUTPUT (first 150 lines):")
                print("="*60)
                for line in lines:
                    print(line)
                
                if "Segmentation fault" in out:
                    print("\n[WARNING] Segmentation fault still occurs!")
                elif "Set face detection confidence threshold" in out:
                    print("\n[SUCCESS] A2 workaround (threshold) executed!")
                    if "TDL setup:" in out:
                        print("[SUCCESS] A1/A3 workarounds (timeout/handle) also active!")
                
                return 0
            else:
                print(f"\n    [ERROR] Build failed with code {rc}")
                if err:
                    print(f"    {err[:300]}")
                return 1
        else:
            print("\n    [ERROR] No compiler found on device")
            print("    Device may need gcc/build tools installed")
            return 1
            
    except Exception as e:
        print(f"\n[FATAL] {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        sftp.close()
        ssh.close()

if __name__ == "__main__":
    sys.exit(main())
