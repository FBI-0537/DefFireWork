# Dockerfile — FireControlApp 的 armhf 交叉编译环境（统一版）
#
# 设计目的
#   把"交叉编译环境"从个人机器搬进镜像：任何人 docker build 出来的环境完全一致，
#   不再依赖各自的 ~/.local、Docker Desktop 缓存或手写 sysroot。
#
# 为什么是 amd64 镜像而不是 armhf 镜像
#   这是**交叉编译**环境，不是运行环境。镜像跑在开发机上（amd64），产出 armhf 二进制。
#   所以不需要 binfmt/qemu 模拟，构建也快。
#
# 为什么用 Debian 的 gcc-arm-linux-gnueabihf 而不是自己拼 sysroot
#   Debian 官方源里就有交叉编译器，配合 multiarch 的 :armhf 库包：
#     头文件  → /usr/include/                 (与宿主共用路径)
#     ARM 库  → /usr/lib/arm-linux-gnueabihf/ (与宿主的 x86_64-linux-gnu 天然隔离)
#   交叉编译器默认就搜多架构目录，所以不必再手工维护
#   include/lib 列表 —— 版本永远和 Debian 仓库一致，不会漂移。
#
# 目标平台
#   正点原子 ATK-DLMP135 (STM32MP135, Cortex-A7) + Debian 12 bookworm armv7l
#   镜像用 bookworm 与目标板同版本，glibc 不会错配。
#
# 用法
#   docker build -t firecontrol-armhf:bookworm armhf-toolchain/
#   ./armhf-toolchain/build-armhf.sh          # Linux / macOS
#   .\armhf-toolchain\build-armhf.ps1         # Windows (Docker Desktop)
#
# ⚠️ 状态说明
#   本文件在编写时**未能在作者的开发机上实测**（该机无法访问 docker.io 拉取
#   debian 基础镜像）。语法与包名均已核对，但请以第一次 docker build 的结果为准。
#   构建结束后镜像内的自检脚本会编译一个含 X11 + freetype 的测试程序，
#   自检失败会让 build 直接失败 —— 所以"构建成功"本身就意味着工具链可用。

# 基础镜像可覆盖 —— 网络访问不了 docker.io 时改用镜像站:
#     docker build --build-arg BASE=docker.m.daocloud.io/library/debian:bookworm-slim \
#                  -t firecontrol-armhf:bookworm armhf-toolchain/
ARG BASE=debian:bookworm-slim
FROM ${BASE}

# 非交互式 apt，避免构建时卡在配置界面
ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

# ---------------------------------------------------------------------------
# 1) 交叉编译器 + 构建工具
# ---------------------------------------------------------------------------
RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabihf \
        g++-arm-linux-gnueabihf \
        binutils-arm-linux-gnueabihf \
        cmake \
        make \
        ninja-build \
        pkg-config \
        ca-certificates; \
    rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# 2) armhf 多架构：GUI 要的 X11 与 freetype 开发库
#
#    LVGL 的显示/输入后端用 X11，中文字形用 FreeType，所以这两样必须在。
#    只装 :armhf —— 不需要宿主(amd64)版本。
#
#    注意 pkg-config 的 .pc 文件由 :armhf 包装到 /usr/lib/arm-linux-gnueabihf/
#    pkgconfig/，那个目录不在默认 PKG_CONFIG_PATH 里，所以要显式加上；
#    PKG_CONFIG_LIBDIR 屏蔽宿主 .pc，避免把 x86-64 的路径带进来。
# ---------------------------------------------------------------------------
RUN set -eux; \
    dpkg --add-architecture armhf; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        libx11-dev:armhf \
        libfreetype-dev:armhf; \
    rm -rf /var/lib/apt/lists/*

ENV PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig
ENV PKG_CONFIG_SYSROOT_DIR=/

# ---------------------------------------------------------------------------
# 3) 镜像内自检
#
#    构建期就把"能不能交叉编译出带 X11 + freetype 的 armhf 程序"验掉。
#    自检不过 → 镜像构建失败，不会留一个看似可用实则残缺的环境。
#
#    三个坑(构建期逐个抓出来的, 留着注释免得后人重犯):
#      a) freetype 头文件在 /usr/include/freetype2, 必须显式 -I,
#         否则报 ft2build.h: No such file or directory
#      b) 不能用 `file` 命令 —— bookworm-slim 没装它。改用 readelf(binutils 自带),
#         直接读 ELF 头反而更准: 一次确认 32 位 + ARM + 硬浮点 ABI
#      c) 这段刻意**不用任何 shell 变量**: Dockerfile 的 RUN 里 `$` 会被构建器
#         先做变量替换, `$$` 在不同构建器(podman/docker)下行为不一致 ——
#         实测 podman 把 `$$(cmd)` 变成了 PID 拼接。改成直接管道, 无转义问题。
# ---------------------------------------------------------------------------
RUN set -eux; \
    printf '%s\n' \
      '#include <X11/Xlib.h>' \
      '#include <ft2build.h>' \
      '#include FT_FREETYPE_H' \
      'int main(void){' \
      '    Display *d = XOpenDisplay(0); (void)d;' \
      '    FT_Library lib; FT_Init_FreeType(&lib);' \
      '    return 0;' \
      '}' > /tmp/probe.c; \
    arm-linux-gnueabihf-gcc -O2 -I/usr/include/freetype2 \
        /tmp/probe.c -o /tmp/probe -lX11 -lfreetype; \
    arm-linux-gnueabihf-readelf -h /tmp/probe | grep -E 'Class:|Machine:|Flags:' | sed 's/^/    /'; \
    arm-linux-gnueabihf-readelf -h /tmp/probe | grep -q 'ELF32'          || { echo "FAIL: 不是 32 位 ELF"; exit 1; }; \
    arm-linux-gnueabihf-readelf -h /tmp/probe | grep -q 'Machine:.*ARM'  || { echo "FAIL: 不是 ARM"; exit 1; }; \
    arm-linux-gnueabihf-readelf -h /tmp/probe | grep -q 'hard-float ABI' || { echo "FAIL: 缺硬浮点 ABI"; exit 1; }; \
    rm -f /tmp/probe /tmp/probe.c; \
    echo "=== 自检 1/2 通过: 能交叉编译 X11 + FreeType 的 armhf 程序 ==="; \
    pkg-config --exists x11       || { echo "FAIL: pkg-config 查不到 x11"; exit 1; }; \
    pkg-config --exists freetype2 || { echo "FAIL: pkg-config 查不到 freetype2"; exit 1; }; \
    echo "=== 自检 2/2 通过: pkg-config 能查到 armhf 的 x11/freetype2 ==="; \
    echo -n "  x11       : "; pkg-config --modversion x11; \
    echo -n "  freetype2 : "; pkg-config --modversion freetype2; \
    echo -n "  x11 cflags: "; pkg-config --cflags x11; \
    echo -n "  ft2 cflags: "; pkg-config --cflags freetype2; \
    arm-linux-gnueabihf-gcc --version | head -1

# 镜像内记录工具链指纹，供产物标记使用（见 build-armhf.sh）
RUN set -eux; \
    { \
      echo "image      = firecontrol-armhf:bookworm"; \
      echo "base       = debian:bookworm-slim"; \
      echo "compiler   = $(arm-linux-gnueabihf-gcc -dumpversion)"; \
      echo "triplet    = $(arm-linux-gnueabihf-gcc -dumpmachine)"; \
      echo "cmake      = $(cmake --version | head -1 | awk '{print $3}')"; \
      echo "built_at   = $(date -u +%Y-%m-%dT%H:%M:%SZ)"; \
    } > /etc/firecontrol-toolchain.txt; \
    cat /etc/firecontrol-toolchain.txt

WORKDIR /work
