#!/usr/bin/env bash
# build.sh — FireControlApp 便捷构建
#
# 用法:
#   ./build.sh native     原生 x86-64: 配置 + 构建 + ctest
#   ./build.sh run        原生构建后运行【控制台程序】(不开窗口)
#   ./build.sh gui        原生构建后打开【GUI 窗口】(1024x600 窗口模式)
#   ./build.sh gui-full   原生 GUI, 全屏
#   ./build.sh armhf      交叉编译纯逻辑层 + 控制台程序 (Zig, 无 GUI)
#   ./build.sh gui-armhf  交叉编译 GUI (Docker, 需 docker/podman)
#   ./build.sh all        原生 + armhf
#   ./build.sh clean      删除构建目录
#   ./build.sh verify     只做 armhf 产物结构验证

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

NATIVE_DIR="build"
# armhf 产物只有一个目录: Zig 版(do_armhf)和 Docker 版(do_gui_armhf)都写这里。
# 两个版本不会同时存在, 切换时 CMake 的工具链指纹检查会给出警告(见 CMakeLists.txt)。
# 注意: armhf-toolchain/build-armhf.sh 里写死了 build-armhf, 不要改成别的名字,
#       否则这里找不到产物。
ARMHF_DIR="build-armhf"
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
    echo "    控制台程序 : $NATIVE_DIR/deffire-dev       (打印 hallo world)"
    echo "    图形界面   : $NATIVE_DIR/deffire-gui-dev   (开窗口, 需 DISPLAY)"
}

# 控制台程序 —— 注意它不开窗口
do_run() {
    c_info "======== 控制台程序 (不开窗口) ========"
    do_native
    c_info "-------- 运行 deffire-dev --------"
    ./"$NATIVE_DIR"/deffire-dev
}

# GUI —— 真正开窗口的那个
do_gui() {
    local mode="${1:-windowed}"
    c_info "======== 原生 GUI (x86-64) ========"
    need_native

    # 只构建 GUI 目标, 不用等控制台程序和测试
    cmake -B "$NATIVE_DIR" -S . -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$NATIVE_DIR" -j "$JOBS" --target deffire-gui-dev

    local arg="--windowed"
    [[ "$mode" == "full" ]] && arg=""
    [[ "$mode" == "windowed" ]] && arg="--windowed"

    if [[ -z "${DISPLAY:-}" ]]; then
        c_err "✗ DISPLAY 未设置, 开不了窗口"
        c_err "  在图形会话的终端里跑, 或先 export DISPLAY=:0"
        exit 1
    fi

    c_info "-------- 启动窗口 $arg --------"
    exec ./"$NATIVE_DIR"/deffire-gui-dev $arg
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
    local exe="$ARMHF_DIR/deffire-dev"

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
    # GUI (deffire-gui-dev) 依赖 libX11, 而 Zig 不带任何 X11 头文件,
    # 交叉编译需要目标板的完整 ARM sysroot —— 所以它被自动跳过了。
    cat <<'NOTE'

    ⚠️ 注意: 这里没有产出 GUI (deffire-gui-dev)

      交叉编译只覆盖: libfirecontrol.a (纯逻辑) + deffire-dev (控制台)
      GUI 依赖 libX11, 而 Zig 不带 X11 头文件, 交叉编译需要目标板的
      ARM 版 libX11 sysroot —— 所以 CMake 自动跳过它。

      GUI 必须在板子上原生编译:
          sudo apt install g++ make pkg-config libx11-dev
          cmake -B build -S . && cmake --build build -j
          DISPLAY=:0 ./build/deffire-gui-dev

      另外: 上面这个 ARM 控制台程序在板子上也跑不了 —— 它是 musl 静态链接的,
      而板子是 Debian/glibc。Debian 上直接用板上 g++ 编译即可。
NOTE
}

do_clean() {
    # build-gui-armhf 是**统一到 Docker 之前** GUI 交叉编译的输出目录
    # (见 c80b177 的 do_gui_armhf: cmake -B "$GUI_ARMHF_DIR")。现在不再产生它,
    # 但旧检出里可能留着, 一并清掉 —— 里面是过时工具链编出来的东西, 留着只会误导。
    rm -rf "$NATIVE_DIR" "$ARMHF_DIR" build-gui-armhf
    c_ok "✓ 已清理 $NATIVE_DIR $ARMHF_DIR build-gui-armhf(遗留)"
}

# ---------------------------------------------------------------------------
# 交叉编译 GUI (armhf)
#
# 统一走 armhf-toolchain 的 Docker 环境 —— 环境由仓库里的 Dockerfile 定义,
# 任何人构建结果一致, 产物带工具链指纹(TOOLCHAIN.txt), 便于隔离排查。
#
# 为什么不在这里直接拼 cmake 命令:
#   GUI 需要 armhf 版 X11/freetype, 且 pkg-config 必须只查 armhf 的 .pc 文件。
#   这些细节封在 armhf-toolchain/ 里, 外层只调它一个入口。
#
# 和 do_armhf 的区别:
#   do_armhf  → 纯逻辑层 + 控制台, 用 Zig, 不需要外部库
#   do_gui_armhf → GUI, 需要 X11/freetype, 走 Docker
# ---------------------------------------------------------------------------
do_gui_armhf() {
    local script="$ROOT/armhf-toolchain/build-armhf.sh"

    if [ ! -x "$script" ]; then
        c_err "✗ 找不到 $script"
        exit 1
    fi

    if ! command -v docker >/dev/null 2>&1 && ! command -v podman >/dev/null 2>&1; then
        c_err "✗ 交叉编译 GUI 需要 docker 或 podman"
        c_err "    Fedora:  sudo dnf install podman"
        c_err "    其它:    https://docs.docker.com/get-docker/"
        c_err ""
        c_err "  为什么需要容器: GUI 依赖 armhf 版 X11/freetype, 这套环境由"
        c_err "  armhf-toolchain/Dockerfile 定义, 保证任何人构建结果一致。"
        c_err "  详见 armhf-toolchain/README.md"
        exit 1
    fi

    c_info "======== 交叉编译 GUI (armhf), 走 Docker ========"
    "$script" "$@"

    local exe="$ARMHF_DIR/deffire-gui-dev"
    [ -f "$exe" ] || { c_err "✗ 没有产出 $exe"; exit 1; }

    if [ -f "$ARMHF_DIR/TOOLCHAIN.txt" ]; then
        echo
        c_info "-------- 工具链指纹 (产物隔离用) --------"
        sed 's/^/    /' "$ARMHF_DIR/TOOLCHAIN.txt"
    fi

    echo
    echo "    上板前校验:  ./armhf-toolchain/verify-on-board.sh root@<板子IP>"
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
