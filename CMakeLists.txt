cmake_minimum_required(VERSION 3.20)

project(FireControlApp
    VERSION 1.0.0
    DESCRIPTION "FireControlApp"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 全局编译选项
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

add_compile_options(-Wall -Wextra)

# 导出 compile_commands.json, 供 VS Code 的 C/C++ 扩展精确解析每个文件的
# 编译参数(标准、宏、头文件路径)。这比手写 includePath 靠谱得多。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# 是否交叉编译
#
# CMAKE_CROSSCOMPILING 只在用了 toolchain file 时为真。
# ---------------------------------------------------------------------------
set(IS_CROSS_BUILD OFF)
if(CMAKE_CROSSCOMPILING)
    set(IS_CROSS_BUILD ON)
elseif(CMAKE_CXX_COMPILER_TARGET MATCHES "arm|aarch64|mips|riscv")
    set(IS_CROSS_BUILD ON)
endif()

# ---------------------------------------------------------------------------
# 业务逻辑库
#
# 把逻辑放库里的好处: 主程序和单元测试链接同一份代码,
# 而且这一层可以交叉编译到 armhf (不依赖任何平台特有库)。
# ---------------------------------------------------------------------------
add_library(firecontrol
    src/greeting.cpp
)
target_include_directories(firecontrol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# 可执行文件
# ---------------------------------------------------------------------------
add_executable(halloworld
    src/main.cpp
)
target_link_libraries(halloworld PRIVATE firecontrol)

# ---------------------------------------------------------------------------
# armhf 产物的瘦身
#
# 必须在链接期加 -s。原因: 本机的 binutils 只编译了 x86 BFD 后端, 不认 ARM,
# 所以事后 strip 不了 (arm-linux-gnueabihf-strip 会报不支持); Zig 也没有
# strip 子命令。只能让链接器直接丢掉符号表。
# 不加这一条, Release 产物会带 debug_info, 大约 5.7 MB; 加了之后约 300 KB。
# ---------------------------------------------------------------------------
if(IS_CROSS_BUILD)
    target_link_options(halloworld PRIVATE -s)
    message(STATUS "armhf 优化: 链接期 -s 剥离符号 (本机无法事后 strip)")
endif()

# ---------------------------------------------------------------------------
# GUI 可执行文件 —— 显示 halloworld 的 X11 窗口
#
# X11 是外部库, 不是内核 UAPI: Zig 一个 X11 头文件都不带。所以:
#
#   WITH_GUI=AUTO (默认)
#       原生构建   → 找系统 X11, 找到就编
#       交叉编译   → 直接跳过, 因为普通 armhf 工具链没有 libX11 sysroot
#
#   WITH_GUI=ON
#       强制尝试。配合 ~/Code/armhf-sysroot/toolchain-armhf-x11.cmake 使用 ——
#       那个 toolchain 带了 ARM 版 libX11, 所以交叉编译 GUI 是可行的:
#           ./build.sh gui-armhf
#
# 命名说明: 不能叫 halloworld, 上面那个控制台程序已经占了。
# ---------------------------------------------------------------------------
set(WITH_GUI "AUTO" CACHE STRING "是否构建 GTK/X11 GUI (ON / OFF / AUTO)")
set_property(CACHE WITH_GUI PROPERTY STRINGS ON OFF AUTO)

if(WITH_GUI STREQUAL "AUTO")
    if(IS_CROSS_BUILD)
        set(_gui_attempt FALSE)
    else()
        set(_gui_attempt TRUE)
    endif()
elseif(WITH_GUI)
    set(_gui_attempt TRUE)
else()
    set(_gui_attempt FALSE)
endif()

if(NOT _gui_attempt)
    set(HAVE_GUI FALSE)
    if(IS_CROSS_BUILD)
        message(STATUS "GUI: 已跳过 —— 交叉编译默认不编 GUI (需要 WITH_GUI=ON 和 X11 sysroot)")
    else()
        message(STATUS "GUI: 已跳过 (WITH_GUI=OFF)")
    endif()
else()
    # Xft 是必须的: X11 核心位图字体只有 ISO-8859-1, 画不出汉字。
    #
    # 用 pkg-config 而不是 CMake 的 FindX11:
    #   FindX11 只给出 Xft 自己的头文件路径, 不给它传递依赖的路径
    #   (freetype2 的 ft2build.h 在 /usr/include/freetype2), 会编译失败。
    #   pkg-config 的 Requires/Requires.private 链能正确处理这些。
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        # 必须显式写 x11: xft.pc 把 x11 放在 Requires.private 里, 而
        # `pkg-config --libs` 默认不输出私有依赖, 只写 xft 会链接失败报
        # "DSO missing from command line"。
        pkg_check_modules(XFT QUIET xft x11)
    endif()

    if(XFT_FOUND)
        set(HAVE_GUI TRUE)
        add_executable(halloworld-gui
            src/gui.cpp
        )
        target_include_directories(halloworld-gui PRIVATE ${XFT_INCLUDE_DIRS})
        target_compile_options(halloworld-gui PRIVATE ${XFT_CFLAGS_OTHER})
        target_link_directories(halloworld-gui PRIVATE ${XFT_LIBRARY_DIRS})
        target_link_libraries(halloworld-gui PRIVATE ${XFT_LIBRARIES})

        # 交叉编译时同样要链接期 -s。
        # 本机无法事后 strip ARM (binutils 只编了 x86 BFD 后端), 所以瘦身只能
        # 在链接时做。不加这一条产物约 100 KB。
        if(IS_CROSS_BUILD)
            target_link_options(halloworld-gui PRIVATE -s)
        endif()

        message(STATUS "GUI: Xft ${XFT_VERSION} (via pkg-config)")
        message(STATUS "     libs: ${XFT_LIBRARIES}")
        message(STATUS "     incs: ${XFT_INCLUDE_DIRS}")
    else()
        set(HAVE_GUI FALSE)
        set(_why "pkg-config 找不到 xft")
        if(IS_CROSS_BUILD)
            message(FATAL_ERROR
                "交叉编译启用了 GUI (WITH_GUI=ON) 但${_why}。\n"
                "需要用带 sysroot 的 toolchain:\n"
                "    cmake -B build -S . \\\n"
                "          -DCMAKE_TOOLCHAIN_FILE=\$HOME/Code/armhf-sysroot/toolchain-armhf-x11.cmake\n"
                "或者直接用: ./build.sh gui-armhf")
        else()
            message(WARNING
                "找不到 Xft 开发文件, 跳过 GUI 目标。\n"
                "    Fedora:        sudo dnf install libXft-devel freetype-devel fontconfig-devel\n"
                "    Debian/Ubuntu: sudo apt install libxft-dev libfreetype-dev libfontconfig-dev")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# 单元测试
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    if(IS_CROSS_BUILD)
        message(STATUS "测试: 已跳过 —— 交叉编译产物无法在构建机上执行")
    else()
        add_executable(test_greeting tests/test_greeting.cpp)
        target_link_libraries(test_greeting PRIVATE firecontrol)

        add_test(NAME greeting COMMAND test_greeting)
    endif()
endif()

# ---------------------------------------------------------------------------
# 构建信息
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "==================== 构建配置 ====================")
message(STATUS "  编译器      : ${CMAKE_CXX_COMPILER}")
message(STATUS "  编译器 ID   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ 标准    : ${CMAKE_CXX_STANDARD}")
message(STATUS "  目标架构    : ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  交叉编译    : ${IS_CROSS_BUILD}")
message(STATUS "  构建类型    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  单元测试    : ${BUILD_TESTING}")
message(STATUS "  构建 GUI    : ${HAVE_GUI}")
message(STATUS "==================================================")
message(STATUS "")
