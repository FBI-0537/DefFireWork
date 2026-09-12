# build-armhf.ps1 — 用 Docker 交叉编译 FireControlApp 到 armhf (Windows)
#
# 用法 (PowerShell, 在仓库根目录或任意位置):
#   .\armhf-toolchain\build-armhf.ps1              构建
#   .\armhf-toolchain\build-armhf.ps1 -Rebuild     先重建镜像再构建
#   .\armhf-toolchain\build-armhf.ps1 -Shell       进容器交互 (调试工具链用)
#
# 需要 Docker Desktop 正在运行。
# 产物在 build-armhf\, 其中 TOOLCHAIN.txt 记录工具链指纹(见文件末尾说明)。

[CmdletBinding()]
param(
    [switch]$Rebuild,
    [switch]$Shell,
    [string]$Image = "firecontrol-armhf:bookworm"
)

$ErrorActionPreference = "Stop"

function Info($m) { Write-Host $m -ForegroundColor Cyan }
function Ok($m)   { Write-Host $m -ForegroundColor Green }
function Fail($m) { Write-Host $m -ForegroundColor Red }

# --- 仓库根目录 = 本脚本所在目录的上一级 ---
$Here       = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $Here
$BuildDir   = Join-Path $ProjectRoot "build-armhf"

# --- 检查 docker ---
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Fail "找不到 docker。请安装并启动 Docker Desktop:"
    Fail "    https://docs.docker.com/desktop/install/windows-install/"
    exit 1
}

$null = & docker info 2>&1
if ($LASTEXITCODE -ne 0) {
    Fail "Docker 没有在运行。启动 Docker Desktop 后重试。"
    exit 1
}

# --- 镜像 ---
$imageExists = $true
$null = & docker image inspect $Image 2>&1
if ($LASTEXITCODE -ne 0) { $imageExists = $false }

if ($Rebuild -or (-not $imageExists)) {
    if ($Rebuild) { Info "=== 重建镜像 $Image ===" }
    else          { Info "=== 镜像 $Image 不存在, 开始构建 ===" }
    Info "    (首次较慢; 之后除非 -Rebuild 否则复用)"
    & docker build -t $Image $Here
    if ($LASTEXITCODE -ne 0) { Fail "镜像构建失败"; exit 1 }
}

# --- 交互模式 ---
if ($Shell) {
    Info "=== 进入容器 ($Image) ==="
    & docker run --rm -it -v "${ProjectRoot}:/work" -w /work $Image /bin/bash
    exit $LASTEXITCODE
}

# --- 构建 ---
if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }

Info "=== 容器内交叉编译 (docker / $Image) ==="
Info "    挂载: $ProjectRoot -> /work"
& docker run --rm `
    -v "${ProjectRoot}:/work" `
    -w /work `
    $Image `
    bash -c "set -e; cmake -B build-armhf -S /work -DCMAKE_TOOLCHAIN_FILE=/work/armhf-toolchain/toolchain-docker-armhf.cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DWITH_GUI=ON; cmake --build build-armhf -j `$(nproc)"

if ($LASTEXITCODE -ne 0) { Fail "构建失败"; exit 1 }

# --- 产物指纹 ---
Info "=== 写工具链指纹 ==="
$fp = & docker run --rm $Image cat /etc/firecontrol-toolchain.txt
$stamp = @(
    "# 本目录产物由以下工具链生成 —— 出问题时先看这里确认来源"
    $fp
    "host_runtime = docker-desktop (windows)"
    "built_local  = $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
)
$stamp | Set-Content -Path (Join-Path $BuildDir "TOOLCHAIN.txt") -Encoding UTF8
$stamp | ForEach-Object { "    $_" }

# --- 结果 ---
Write-Host ""
Info "=== 产物 ==="
$found = $false
foreach ($exe in @("halloworld-gui", "halloworld")) {
    $f = Join-Path $BuildDir $exe
    if (Test-Path $f) {
        $sz = (Get-Item $f).Length
        Write-Host ("  {0,-20} {1,9} 字节" -f $exe, $sz)
        $found = $true
    }
}
if (-not $found) { Fail "build-armhf\ 里没有可执行文件"; exit 1 }

Write-Host ""
Ok "✓ 完成: $BuildDir"
Write-Host @"

  部署到板子:
      scp build-armhf/halloworld-gui root@<板子IP>:~/
      scp build-armhf/TOOLCHAIN.txt  root@<板子IP>:~/
      ssh root@<板子IP> 'DISPLAY=:0 ./halloworld-gui'

  板上自检(确认产物与板子匹配):
      bash armhf-toolchain/verify-on-board.sh root@<板子IP>     # 在 Linux 上
"@
