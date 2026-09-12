# toolchain-armhf.cmake — armhf (arm-linux-musleabihf) 交叉编译工具链
#
# 用法:
#   cmake -B build-armhf -S . \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-armhf.cmake
#   cmake --build build-armhf
#
# 为什么用 musl 而不是 glibc:
#   glibc 目标不支持静态链接 (报 "libc of the specified target requires
#   dynamic linking")。musl 可以静态链接, 产物零依赖, 拷到板子上直接跑。
#
# 编译器是 Zig 0.16 充当的 gcc 包装脚本。

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(ARMHF_TOOLCHAIN_PREFIX "$ENV{HOME}/.local/bin" CACHE PATH "armhf 工具链所在目录")
set(CMAKE_C_COMPILER   "${ARMHF_TOOLCHAIN_PREFIX}/arm-linux-musleabihf-gcc")
set(CMAKE_CXX_COMPILER "${ARMHF_TOOLCHAIN_PREFIX}/arm-linux-musleabihf-g++")

# ---------------------------------------------------------------------------
# 关键: Zig 0.16 的 `-fsyntax-only` 是坏的, 会报 "FileNotFound"。
# CMake 默认用它探测编译器, 不改这里 configure 就会直接失败。
# 让它改用静态库探测 —— 静态库不需要链接, 绕开该问题。
# ---------------------------------------------------------------------------
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 不要在主机路径里找库和头文件, 否则会把 x86-64 的东西混进 ARM 链接
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
