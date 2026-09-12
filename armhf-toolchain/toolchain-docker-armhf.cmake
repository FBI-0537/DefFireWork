# toolchain-armhf.cmake — Docker 容器内的 armhf 交叉编译工具链
#
# 这个文件配合 armhf-toolchain/Dockerfile 使用：在容器里编译 cmake 工程。
# 它**假定依赖由 Debian multiarch 提供**（见 Dockerfile），所以不需要像手工
# sysroot 那样列一堆 include/lib 路径 —— 交叉编译器自己会搜多架构目录：
#     /usr/lib/arm-linux-gnueabihf/     ← ARM 库
#     /usr/include/                     ← 头文件
#
# 用法（在容器内）:
#     cmake -B build-armhf -S /work \
#           -DCMAKE_TOOLCHAIN_FILE=/work/armhf-toolchain/toolchain-armhf.cmake \
#           -DCMAKE_BUILD_TYPE=Release -DWITH_GUI=ON
#     cmake --build build-armhf -j

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# 交叉编译时不要在宿主路径里找东西
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---------------------------------------------------------------------------
# pkg-config：只读 armhf 的 .pc 文件
#
# Debian 的 :armhf 开发包把 .pc 放在 /usr/lib/arm-linux-gnueabihf/pkgconfig/，
# 那个目录不在默认 PKG_CONFIG_PATH 里；同时必须屏蔽宿主(amd64)的 .pc，
# 否则 pkg_check_modules 拿到的是 x86-64 的路径，链接期会报 DSO missing。
#
# 这里用 set(ENV{...}) 而不是 add_compile_options —— 让 pkg-config 子进程继承。
# ---------------------------------------------------------------------------
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/")

# 有些 CMake 的 find_package 会读这些，一并指过去
set(CMAKE_LIBRARY_ARCHITECTURE "arm-linux-gnueabihf")
