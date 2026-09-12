#!/usr/bin/env bash
# build.sh — FireControlApp 便捷构建
#
# 用法:
#   ./build.sh native     原生 x86-64: 配置 + 构建 + ctest
#   ./build.sh run        原生构建后运行【控制台程序】(不开窗口)
#   ./build.sh gui        原生构建后打开【GUI 窗口】(1024x600 窗口模式)
#   ./build.sh gui-full   原生 GUI, 全屏
#   ./build.sh armhf      交叉编译纯逻辑层 + 控制台程序 (无 GUI)
#   ./build.sh gui-armhf  交叉编译 X11 GUI (需要 armhf-sysroot)
#   ./build.sh all        原生 + armhf
#   ./build.sh clean      删除构建目录
#   ./build.sh verify     只做 armhf 产物结构验证

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

NATIVE_DIR="build"
ARMHF_DIR="build-armhf"
GUI_ARMHF_DIR="build-gui-armhf"
TOOLCHAIN="cmake/toolchain-armhf.cmake"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="$(nproc 2>/dev/null || echo 4)"

# readelf 的字段名会被 locale 本地化 (中文下 Machine → 机器), 必须强制 C
export LC_ALL=C

c_ok()   { printf '\033[32m%s\033[0m\n' "$*"; }
c_info() { printf '\033[36m%s\033[0m\n' "$*"; }
c_err()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }

need_native() {
    for t in cmake g++; do
        command -v "$t" >/dev/null 2>&1 || {
            c_err "✗ 缺少 $t"
            c_err "    sudo dnf install gcc gcc-c++ cmake ninja-build"
            exit 1
        }
    done
}

need_armhf() {
    [[ -x "$HOME/.local/bin/arm-linux-musleabihf-g++" ]] || {
        c_err "✗ 缺少 arm-linux-musleabihf-g++"
        c_err "   先运行 armhf-env 检查 Zig 交叉编译环境"
        exit 1
    }
}

do_native() {
    c_info "======== 原生 x86-64 ========"
    need_native
    cmake -B "$NATIVE_DIR" -S . -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$NATIVE_DIR" -j "$JOBS"
    c_info "-------- ctest --------"
    ctest --test-dir "$NATIVE_DIR" --output-on-failure
    c_ok "✓ 完成:"
    echo "    控制台程序 : $NATIVE_DIR/halloworld       (打印 hallo world)"
    echo "    图形界面   : $NATIVE_DIR/halloworld-gui   (开窗口, 需 DISPLAY)"
}

# 控制台程序 —— 注意它不开窗口
do_run() {
    c_info "======== 控制台程序 (不开窗口) ========"
    do_native
    c_info "-------- 运行 halloworld --------"
    ./"$NATIVE_DIR"/halloworld
}

# GUI —— 真正开窗口的那个
do_gui() {
    local mode="${1:-windowed}"
    c_info "======== 原生 GUI (x86-64) ========"
    need_native

    # 只构建 GUI 目标, 不用等控制台程序和测试
    cmake -B "$NATIVE_DIR" -S . -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$NATIVE_DIR" -j "$JOBS" --target halloworld-gui

    local arg="--windowed"
    [[ "$mode" == "full" ]] && arg=""
    [[ "$mode" == "windowed" ]] && arg="--windowed"

    if [[ -z "${DISPLAY:-}" ]]; then
        c_err "✗ DISPLAY 未设置, 开不了窗口"
        c_err "  在图形会话的终端里跑, 或先 export DISPLAY=:0"
        exit 1
    fi

    c_info "-------- 启动窗口 $arg --------"
    exec ./"$NATIVE_DIR"/halloworld-gui $arg
}

do_armhf() {
    c_info "======== armhf 交叉编译 ========"
    need_armhf
    # 注意: 这里显式 WITH_GUI=OFF。do_armhf 用的是不含 X11 sysroot 的普通
    # 工具链, 想编 GUI 请用 ./build.sh gui-armhf。
    cmake -B "$ARMHF_DIR" -S . \
          -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DBUILD_TESTING=OFF \
          -DWITH_GUI=OFF
    cmake --build "$ARMHF_DIR" -j "$JOBS"
    verify_armhf
}

verify_armhf() {
    c_info "-------- armhf 产物验证 --------"
    local exe="$ARMHF_DIR/halloworld"

    [[ -f "$exe" ]] || { c_err "✗ 缺少 $exe"; exit 1; }

    file "$exe" | sed 's/^/    /'
    arm-linux-gnueabihf-readelf -h "$exe" \
        | grep -E 'Class:|Machine:|Flags:' | sed 's/^/    /'
    echo "    体积: $(stat -c %s "$exe") 字节"

    if arm-linux-gnueabihf-readelf -h "$exe" | grep -q 'ARM'; then
        c_ok "✓ armhf 构建完成: $exe"
    else
        c_err "✗ 产物不是 ARM 架构"
        exit 1
    fi

    # 这条提示很重要: armhf 交叉编译只覆盖纯逻辑层 + 控制台程序。
    # GUI (halloworld-gui) 依赖 libX11, 而 Zig 不带任何 X11 头文件,
    # 交叉编译需要目标板的完整 ARM sysroot —— 所以它被自动跳过了。
    cat <<'NOTE'

    ⚠️ 注意: 这里没有产出 GUI (halloworld-gui)

      交叉编译只覆盖: libfirecontrol.a (纯逻辑) + halloworld (控制台)
      GUI 依赖 libX11, 而 Zig 不带 X11 头文件, 交叉编译需要目标板的
      ARM 版 libX11 sysroot —— 所以 CMake 自动跳过它。

      GUI 必须在板子上原生编译:
          sudo apt install g++ make pkg-config libx11-dev
          cmake -B build -S . && cmake --build build -j
          DISPLAY=:0 ./build/halloworld-gui

      另外: 上面这个 ARM 控制台程序在板子上也跑不了 —— 它是 musl 静态链接的,
      而板子是 Debian/glibc。Debian 上直接用板上 g++ 编译即可。
NOTE
}

do_clean() {
    rm -rf "$NATIVE_DIR" "$ARMHF_DIR" "$GUI_ARMHF_DIR"
    c_ok "✓ 已清理 $NATIVE_DIR $ARMHF_DIR $GUI_ARMHF_DIR"
}

# ---------------------------------------------------------------------------
# 交叉编译 X11 GUI
#
# 需要 ~/Code/armhf-sysroot 提供的 ARM 版 libX11 (sysroot)。
# 这和 do_armhf 是两件事:
#   do_armhf  → 纯逻辑层, 不需要外部库, Zig 单独就能编
#   do_gui    → 需要 X11, 必须有 sysroot
# ---------------------------------------------------------------------------
do_gui_armhf() {
    c_info "======== 交叉编译 X11 GUI (armhf) ========"

    local sysroot="$HOME/Code/armhf-sysroot"
    [[ -d "$sysroot/sysroot" ]] || {
        c_err "✗ 找不到 X11 sysroot: $sysroot/sysroot"
        c_err "  先建它:"
        c_err "      cd ~/Code/armhf-sysroot"
        c_err "      python3 scripts/fetch-packages.py"
        c_err "      python3 scripts/make-sysroot.py"
        exit 1
    }

    need_native   # 需要 cmake

    cmake -B "$GUI_ARMHF_DIR" -S . \
          -DCMAKE_TOOLCHAIN_FILE="$sysroot/toolchain-armhf-x11.cmake" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DBUILD_TESTING=OFF \
          -DWITH_GUI=ON

    cmake --build "$GUI_ARMHF_DIR" -j "$JOBS"

    local exe="$GUI_ARMHF_DIR/halloworld-gui"
    [[ -f "$exe" ]] || { c_err "✗ 没有产出 $exe"; exit 1; }

    c_info "-------- 产物验证 --------"
    LC_ALL=C file "$exe" | sed 's/^/    /'
    echo "    体积: $(stat -c %s "$exe") 字节"
    arm-linux-gnueabihf-readelf -h "$exe" \
        | grep -E 'Class:|Machine:|Flags:' | sed 's/^/    /'
    echo "    动态依赖:"
    arm-linux-gnueabihf-readelf -d "$exe" 2>/dev/null \
        | grep NEEDED | sed 's/^/      /'

    c_ok "✓ 交叉编译完成: $exe"
    echo
    echo "    拷到板子:  scp $exe root@<板子IP>:~/"
    echo "    板上运行:  DISPLAY=:0 ./halloworld-gui"
}

case "${1:-native}" in
    native) do_native ;;
    run)    do_run ;;
    gui)    do_gui windowed ;;
    gui-full) do_gui full ;;
    armhf)  do_armhf ;;
    gui-armhf) do_gui_armhf ;;
    all)    do_native; echo; do_armhf ;;
    verify) verify_armhf ;;
    clean)  do_clean ;;
    *) c_err "用法: $0 [native|run|gui|gui-full|armhf|gui-armhf|all|verify|clean]"; exit 1 ;;
esac
