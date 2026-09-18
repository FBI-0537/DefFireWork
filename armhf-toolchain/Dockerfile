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
# 状态
#   已在 **Windows + Docker Desktop** 上实测通过（2026-09-12，FBI-0537）。
#   构建结束后镜像内的自检脚本会编译一个含 X11 + freetype 的探针程序并校验
#   硬浮点 ABI，自检失败会让 build 直接失败 —— 所以"构建成功"就意味着工具链可用。
#
#   首次实测修掉了三个会让构建**必然失败**的问题（下面各标注了「实测修复」）:
#     1. 自检没把 pkg-config 的 -I/usr/include/freetype2 带进去 → ft2build.h 找不到
#     2. 自检用了 file，而 debian:bookworm-slim 里没有这个包
#     3. deb.debian.org(Fastly) 会"下到一半断掉" → 默认源换成清华 TUNA

# 基础镜像可覆盖 —— 网络访问不了 docker.io 时改用镜像站:
#     docker build --build-arg BASE=docker.m.daocloud.io/library/debian:bookworm-slim \
#                  -t firecontrol-armhf:bookworm armhf-toolchain/
ARG BASE=debian:bookworm-slim
FROM ${BASE}

# 非交互式 apt，避免构建时卡在配置界面
ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8

# ---------------------------------------------------------------------------
# 0) 软件源
#
# deb.debian.org (Fastly) 在部分网络下会间歇性连不上, 实测到过:
#     E: Failed to fetch http://deb.debian.org/debian/pool/.../libx11-dev_...deb
#        Unable to connect to deb.debian.org:80 [IP: 28.0.0.12 80]
# 表现是"前 10 MB 能下来, 后面直接断", 镜像构建随机失败。
# 默认换成清华 TUNA(国内可达性好)。要用官方源:
#     docker build --build-arg APT_MIRROR=deb.debian.org \
#                  -t firecontrol-armhf:bookworm armhf-toolchain/
# ---------------------------------------------------------------------------
ARG APT_MIRROR=mirrors.tuna.tsinghua.edu.cn
RUN set -eux; \
    if [ "$APT_MIRROR" != "deb.debian.org" ]; then \
        sed -i "s|deb.debian.org|${APT_MIRROR}|g" /etc/apt/sources.list.d/debian.sources; \
    fi; \
    grep -E '^URIs:' /etc/apt/sources.list.d/debian.sources

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
#    libgpiod-dev:armhf 只有 gpio_read_value() 用（读 GPIO 开关量输入，
#    给传感器模块用；板载 LED/蜂鸣器走 sysfs，不需要它）。装在这里是为了让
#    交叉编译产物能链上 armhf 版 libgpiod —— 板子上还要有运行时库 libgpiod2。
#    它同样只装 :armhf。
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
        libfreetype-dev:armhf \
        libgpiod-dev:armhf; \
    rm -rf /var/lib/apt/lists/*

ENV PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig
ENV PKG_CONFIG_SYSROOT_DIR=/

# ---------------------------------------------------------------------------
# 3) 镜像内自检
#
#    构建期就把"能不能交叉编译出带 X11 + freetype + libgpiod 的 armhf 程序"验掉。
#    自检不过 → 镜像构建失败，不会留一个看似可用实则残缺的环境。
#
#    五个坑(构建期逐个抓出来的, 留着注释免得后人重犯):
#      a) 用 pkg-config 取 flags, 不要手写 -I/usr/include/freetype2。
#         pkg-config 会自动带上 freetype2 的 include 路径;
#         手写的话换 Debian 版本/包布局就可能失效。
#      b) 断言 pkg-config 的 -L 里必须含 arm-linux-gnueabihf ——
#         万一 PKG_CONFIG_LIBDIR 配错命中了宿主的 x86-64 .pc, 链接期会链错架构,
#         而那种错误在表面上看起来是"编译成功"。这条断言把静默错链变成显式失败。
#      c) 不用 `file` 命令 —— bookworm-slim 没装它, 没必要为自检多装一个包。
#         readelf(binutils 自带) 直接读 ELF 头反而更准:
#         一次确认 Class=ELF32 / Machine=ARM / hard-float ABI。
#      d) 这段刻意**不用任何 shell 变量**: Dockerfile 的 RUN 里 `$` 会被构建器
#         先做变量替换, `$$` 在不同构建器(podman/docker)下行为不一致 ——
#         实测 podman 把 `$$(cmd)` 变成了 PID 拼接。改成直接管道, 无转义问题。
#      e) 探针刻意调用 libgpiod 的 **v1** API (gpiod_chip_open_by_label):
#         项目代码用的就是 v1 (见 src/usetools.cpp)。将来基础镜像升到 v2 时,
#         这一步会**在镜像构建时就失败**, 而不是等编项目时才报一堆
#         "未声明标识符" —— 那时更难判断是代码问题还是依赖问题。
# ---------------------------------------------------------------------------
RUN set -eux; \
    printf '%s\n' \
      '#include <X11/Xlib.h>' \
      '#include <ft2build.h>' \
      '#include FT_FREETYPE_H' \
      '#include <gpiod.h>' \
      'int main(void){' \
      '    Display *d = XOpenDisplay(0); (void)d;' \
      '    FT_Library lib; FT_Init_FreeType(&lib);' \
      '    struct gpiod_chip *c = gpiod_chip_open_by_label("gpiochip0"); (void)c;' \
      '    return 0;' \
      '}' > /tmp/probe.c; \
    pkg-config --libs x11 freetype2 libgpiod | grep -q 'arm-linux-gnueabihf' \
        || { echo "FAIL: pkg-config 的库路径不含 arm-linux-gnueabihf —— 可能命中了宿主 x86-64 的 .pc"; exit 1; }; \
    arm-linux-gnueabihf-gcc -O2 /tmp/probe.c -o /tmp/probe \
        $(pkg-config --cflags --libs x11 freetype2 libgpiod); \
    arm-linux-gnueabihf-readelf -h /tmp/probe | grep -E 'Class:|Machine:|Flags:' | sed 's/^/    /'; \
    arm-linux-gnueabihf-readelf -h /tmp/probe | grep -q 'ELF32'          || { echo "FAIL: 不是 32 位 ELF"; exit 1; }; \
    arm-linux-gnueabihf-readelf -h /tmp/probe | grep -q 'Machine:.*ARM'  || { echo "FAIL: 不是 ARM"; exit 1; }; \
    arm-linux-gnueabihf-readelf -h /tmp/probe | grep -q 'hard-float ABI' || { echo "FAIL: 缺硬浮点 ABI"; exit 1; }; \
    rm -f /tmp/probe /tmp/probe.c; \
    echo "=== 自检 1/2 通过: 能交叉编译 X11 + FreeType + libgpiod(v1) 的 armhf 程序 ==="; \
    pkg-config --exists x11       || { echo "FAIL: pkg-config 查不到 x11"; exit 1; }; \
    pkg-config --exists freetype2 || { echo "FAIL: pkg-config 查不到 freetype2"; exit 1; }; \
    pkg-config --exists libgpiod  || { echo "FAIL: pkg-config 查不到 libgpiod"; exit 1; }; \
    echo "=== 自检 2/2 通过: pkg-config 能查到 armhf 的 x11/freetype2/libgpiod ==="; \
    echo -n "  x11       : "; pkg-config --modversion x11; \
    echo -n "  freetype2 : "; pkg-config --modversion freetype2; \
    echo -n "  libgpiod  : "; pkg-config --modversion libgpiod; \
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
