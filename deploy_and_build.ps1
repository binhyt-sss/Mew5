# Remote build script for Milk-V Duo device
# This script will copy modified sources, rebuild on device, and test

param(
    [string]$DeviceIP = "192.168.42.1",
    [string]$DeviceUser = "root",
    [string]$Password = "milkv"
)

$SourceDir = "D:\Mew5\duo-tdl-examples"
$SampleDir = "$SourceDir\sample_vi_fd"
$CommonDir = "$SourceDir\common" 

Write-Host "====== Build & Deploy sample_vi_fd with A1-A3 Workarounds ======"
Write-Host "Target: $DeviceUser@$DeviceIP"
Write-Host ""

# Step 1: Create temporary directory on device for sources
Write-Host "[1] Creating work directory on device..."
$cmd = "ssh.exe -o StrictHostKeyChecking=no -o ConnectTimeout=5 root@$DeviceIP `"mkdir -p /mnt/build && cd /mnt/build && pwd`""
Write-Host "  Command: $cmd"

try {
    $result = Invoke-Expression $cmd 2>&1
    Write-Host "  Result: $result"
} catch {
    Write-Host "  ERROR: $_"
    exit 1
}

# Step 2: Copy sample_vi_fd.c
Write-Host "[2] Copying sample_vi_fd.c to device..."
$src = "$SampleDir\sample_vi_fd.c"
$dst = "root@$DeviceIP`:/mnt/build/sample_vi_fd.c"
$cmd = "scp.exe -o StrictHostKeyChecking=no `"$src`" `"$dst`""
Write-Host "  From: $src"
Write-Host "  To: $dst"

try {
    $result = Invoke-Expression $cmd 2>&1
    Write-Host "  OK"
} catch {
    Write-Host "  ERROR: $_"
    exit 1
}

# Step 3: Copy middleware_utils.c
Write-Host "[3] Copying middleware_utils.c to device..."
$src = "$CommonDir\middleware_utils.c"
$dst = "root@$DeviceIP`:/mnt/build/middleware_utils.c"  
$cmd = "scp.exe -o StrictHostKeyChecking=no `"$src`" `"$dst`""
Write-Host "  From: $src"
Write-Host "  To: $dst"

try {
    $result = Invoke-Expression $cmd 2>&1
    Write-Host "  OK"
} catch {
    Write-Host "  ERROR: $_"
    exit 1
}

# Step 4: Copy other source files
Write-Host "[4] Copying supporting source files..."
foreach ($file in @("sample_utils.c", "vi_vo_utils.c")) {
    $src = "$CommonDir\$file"
    $dst = "root@$DeviceIP`:/mnt/build/$file"
    $cmd = "scp.exe -o StrictHostKeyChecking=no `"$src`" `"$dst`""
    Write-Host "  Copying $file..."
    try {
        $result = Invoke-Expression $cmd 2>&1
    } catch {
        Write-Host "    ERROR: $_"
        exit 1
    }
}

# Step 5: Remote build
Write-Host "[5] Building on device..."
$buildcmd = @"
cd /mnt/build && 
export TOOLCHAIN_PREFIX='' &&
export CC='gcc' &&
export CFLAGS='-I/usr/include -I/mnt/shared/duo-tdl-examples/include/system -I/mnt/shared/duo-tdl-examples/include/tdl -O2' &&
export LDFLAGS='-L/mnt/shared/duo-tdl-examples/libs/system/musl_riscv64 -L/mnt/shared/duo-tdl-examples/libs/tdl/cv181x_riscv64 -lpthread -latomic' &&
gcc -c sample_vi_fd.c &&
gcc -c middleware_utils.c &&
gcc -c sample_utils.c &&
gcc -c vi_vo_utils.c &&
gcc *.o -o sample_vi_fd_new &&
ls -lh sample_vi_fd_new
"@

$remotecmd = "ssh.exe -o StrictHostKeyChecking=no root@$DeviceIP `"$buildcmd`""
Write-Host "  Running remote compile..."

try {
    $result = Invoke-Expression $remotecmd 2>&1
    Write-Host $result
    Write-Host "  BUILD_OK"
} catch {
    Write-Host "  ERROR: $_"
    exit 1  
}

# Step 6: Copy binary back
Write-Host "[6] Copying new binary back to Windows..."
$src = "root@$DeviceIP`:/mnt/build/sample_vi_fd_new"
$dst = "$SampleDir\sample_vi_fd_test"
$cmd = "scp.exe -o StrictHostKeyChecking=no `"$src`" `"$dst`""
Write-Host "  From: $src"
Write-Host "  To: $dst"

try {
    $result = Invoke-Expression $cmd 2>&1
    Write-Host "  OK"
} catch {
    Write-Host "  ERROR: $_"
    exit 1
}

Write-Host ""
Write-Host "====== Build Complete ======"
Write-Host "New binary saved to: $dst"
