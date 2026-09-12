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
# GUI 可执行文件 —— LVGL 图形界面 (halloworld-gui)
#
# 维护分工: 这一节的 LVGL 集成由 FBI-0537 负责; 上面的纯逻辑层、控制台程序与
#           cmake/toolchain-armhf.cmake 来自 Skywindfox, 改动时保留其注释与编译方式。
#
# 界面用 LVGL v9.2, 源码就在 third_party/lvgl (随仓库一起走, 不依赖网络)。
#
# 显示后端选的是 LVGL 自带的 X11 驱动, 而不是它更"嵌入式"的 framebuffer 驱动:
# 板上跑着 Xorg, /dev/fb0 归它管, 直接写 framebuffer 会和 Xorg 抢同一块屏。
# 中文靠 LVGL 的 FreeType 后端运行时加载板上的文泉驿正黑 —— 所以链接期需要
# libX11 + libfreetype (两者在板上都是现成的)。
#
#   WITH_GUI=AUTO (默认)
#       原生构建   → 找得到 x11/freetype2 就编
#       交叉编译   → 直接跳过 (普通 armhf 工具链里没有 X11/freetype 的 ARM 头文件和库)
#
#   WITH_GUI=ON
#       强制尝试。交叉编译用 armhf-toolchain 那套 Debian multiarch 镜像
#       (Dockerfile 里已装 libx11-dev:armhf / libfreetype-dev:armhf):
#           .\armhf-toolchain\build-armhf.ps1
#
# 命名说明: 不能叫 halloworld, 上面那个控制台程序已经占了。
# ---------------------------------------------------------------------------
set(WITH_GUI "AUTO" CACHE STRING "是否构建 LVGL GUI (ON / OFF / AUTO)")
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
        message(STATUS "GUI: 已跳过 —— 交叉编译默认不编 GUI (需要 WITH_GUI=ON 和 ARM 版 X11/freetype)")
    else()
        message(STATUS "GUI: 已跳过 (WITH_GUI=OFF)")
    endif()
else()
    # 用 pkg-config 而不是 CMake 的 FindX11:
    #   FindX11 只给出 X11 自己的头文件路径, 不给它传递依赖的路径
    #   (freetype2 的 ft2build.h 在 /usr/include/freetype2), 会编译失败。
    #   pkg-config 的 Requires 链能正确处理这些。
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        # x11 = LVGL 的 X11 显示/输入驱动; freetype2 = 中文字形
        pkg_check_modules(GUI_DEPS QUIET x11 freetype2)
    endif()

    if(GUI_DEPS_FOUND)
        # --- LVGL ---
        # LV_CONF_PATH 必须在 add_subdirectory 之前设置: lvgl 自己的 CMake 会把它
        # 变成编译宏 (-DLV_CONF_PATH=...)。找不到这个文件时 LVGL 会退回全默认配置
        # (16 位色、没开 X11/FreeType), 编出来的界面是另一回事。
        set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")

        # 只编 src/: 不编 examples/demos, 也不编内置的 ThorVG 矢量图库。用不到,
        # 编了只是让构建变慢。
        set(LV_CONF_BUILD_DISABLE_EXAMPLES ON)
        set(LV_CONF_BUILD_DISABLE_DEMOS ON)
        set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON)

        set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl"
            CACHE PATH "LVGL 源码目录")
        if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
            message(FATAL_ERROR
                "找不到 LVGL 源码: ${LVGL_DIR}/lvgl.h\n"
                "third_party/lvgl 是随仓库一起提交的, 检查是不是被清掉了")
        endif()
        add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)

        # LVGL 是第三方代码, 不参与本项目"零警告"的要求。
        # 顺带把 x11/freetype 的头文件路径挂上去: LVGL 的 FreeType 后端要
        # ft2build.h (在 /usr/include/freetype2, 不是默认搜索路径), X11 后端要
        # X11/Xlib.h。少了这几行编译 lv_freetype.c 时会报 ft2build.h 找不到。
        target_compile_options(lvgl PRIVATE -w ${GUI_DEPS_CFLAGS_OTHER})
        target_include_directories(lvgl PRIVATE ${GUI_DEPS_INCLUDE_DIRS})

        set(HAVE_GUI TRUE)
        add_executable(halloworld-gui
            src/gui.cpp
        )
        target_include_directories(halloworld-gui PRIVATE ${GUI_DEPS_INCLUDE_DIRS})
        target_compile_options(halloworld-gui PRIVATE ${GUI_DEPS_CFLAGS_OTHER})
        target_link_directories(halloworld-gui PRIVATE ${GUI_DEPS_LIBRARY_DIRS})
        target_link_libraries(halloworld-gui PRIVATE
            lvgl::lvgl                 # LVGL 本体
            ${GUI_DEPS_LIBRARIES}      # libX11 + libfreetype
            pthread m                  # X11 后端用线程模拟 tick; LVGL 内部用 libm
        )

        # 交叉编译时同样要链接期 -s。
        # 本机无法事后 strip ARM (binutils 只编了 x86 BFD 后端), 所以瘦身只能
        # 在链接时做。
        if(IS_CROSS_BUILD)
            target_link_options(halloworld-gui PRIVATE -s)
        endif()

        message(STATUS "GUI: LVGL (third_party) + pkg-config ${GUI_DEPS_VERSION}")
        message(STATUS "     libs: ${GUI_DEPS_LIBRARIES}")
        message(STATUS "     incs: ${GUI_DEPS_INCLUDE_DIRS}")
    else()
        set(HAVE_GUI FALSE)
        set(_why "pkg-config 找不到 x11 / freetype2")
        if(IS_CROSS_BUILD)
            message(FATAL_ERROR
                "交叉编译启用了 GUI (WITH_GUI=ON) 但${_why}。\n"
                "要用装了 ARM 版 X11/freetype 开发包的交叉环境, 例如:\n"
                "    .\\armhf-toolchain\\build-armhf.ps1")
        else()
            message(WARNING
                "找不到 x11 / freetype2 开发文件, 跳过 GUI 目标。\n"
                "    Fedora:        sudo dnf install libX11-devel freetype-devel\n"
                "    Debian/Ubuntu: sudo apt install libx11-dev libfreetype-dev")
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
