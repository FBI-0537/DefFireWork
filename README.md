# FireControlApp

## 致谢

本项目的完成离不开以下贡献者与开源项目，在此致谢。

**贡献者**

- [@Skywindfox](https://github.com/Skywindfox) —— 项目发起、硬件平台搭建、整体设计与验证
- [@FBI-0537](https://github.com/FBI-0537) —— 参与开发，验证了 fork 协作链路（[PR #1](https://github.com/Skywindfox/DefFireWork/pull/1)）

**开源项目与技术**

- [Zig](https://ziglang.org/) —— 充当 armhf 交叉编译器，让本机无需 root 即可完成 ARM 构建
- [X.Org / Xlib / Xft](https://www.x.org/) —— 图形界面与文字渲染的基础
- [FreeType](https://freetype.org/) / [fontconfig](https://www.freedesktop.org/wiki/Software/fontconfig/) —— TrueType 渲染与字体匹配
- [文泉驿](http://wenq.org/)（WenQuanYi）—— 板上的中文字体
- [Noto Sans CJK](https://fonts.google.com/noto) —— 开发机上的中文字体
- [CMake](https://cmake.org/) / [GitHub](https://github.com/) —— 构建系统与代码托管

**硬件与系统**

- 正点原子 ATK-DLMP135 开发板（STM32MP135）
- Debian GNU/Linux 12 (bookworm) armv7l

## 前言

- 本项目旨在完成个人的省级大创项目的目的，本仓库仅作个人存储，不建议对外使用
- 本项目的目的是完成一个在 STM32MP135 环境下，正点原子开发板作为硬件、以自编译的Debian Linux软件开发的智能楼宇消防系统。
- 主要功能模块： （暂未填写）
- 当前完成度 ： 
  |描述|进度|
  |---|---|
  | 板子上硬件基础测试 | 100% |
  | 系统替换 | 100% |
  | 系统环境 | 100% |
  | 传感器 | 0% |
  | PCB | 0% |
  | 触发器 | 0% |
  | 中控软件 | 5% |

> **【这里留空 —— 请自行描述本项目的用途】**
>
> 建议写清楚这些，后面接手的人（或 AI）会省很多事：
> - 这个项目要解决什么问题、跑在什么场景下
> - 主要功能模块
> - 与板子上其他程序/硬件的关系
> - 当前的完成度

---

## 目录

- [1. 项目构成](#1-项目构成)
- [2. 开发环境](#2-开发环境)
- [3. 交叉编译工具链](#3-交叉编译工具链)
- [4. X11 交叉编译环境（armhf-sysroot）](#4-x11-交叉编译环境armhf-sysroot)
- [5. 目标板环境](#5-目标板环境)
- [6. 构建与运行](#6-构建与运行)
- [7. VS Code 配置](#7-vs-code-配置)
- [8. 目录结构](#8-目录结构)
- [9. 设计说明](#9-设计说明)
- [10. 已知问题与踩坑记录](#10-已知问题与踩坑记录)
- [11. 实测结果](#11-实测结果)
- [12. 待办](#12-待办)

---

## 1. 项目构成

一个 CMake 工程，产出三个可执行文件：

| 目标 | 源文件 | 说明 | 能否交叉编译 |
|---|---|---|---|
| `halloworld` | `src/main.cpp` | **控制台程序**，打印 `hallo world`。不开窗口 | ✅ 能 |
| `halloworld-gui` | `src/gui.cpp` | **X11 图形界面**，全屏/窗口显示 `halloworld` | ✅ 能（需 X11 sysroot，见第 4 节） |
| `test_greeting` | `tests/test_greeting.cpp` | 单元测试，`ctest` 调用 | ❌ 交叉产物跑不了 |
| `libfirecontrol.a` | `src/greeting.cpp` | 纯逻辑层静态库，上面两个程序都链接它 | ✅ 能 |

**⚠️ 命名注意**：`halloworld` 拼写是 `hallow` 而非 `hollow`，沿用最初的文件名。
GUI 之所以叫 `halloworld-gui`，是因为 `halloworld` 这个名字已被控制台程序占用。

---

## 2. 开发环境

当前开发机（实测值，非估计）：

| 项目 | 值 |
|---|---|
| 发行版 | **Fedora Linux 44 (Workstation Edition)** |
| 内核 | 7.2.4-200.fc44.x86_64 |
| 架构 | x86_64 |
| 桌面 | GNOME on Wayland（带 Xwayland，可跑 X11 程序预览） |

### 编译工具

| 工具 | 版本 | 用途 |
|---|---|---|
| gcc / g++ | **16.2.1** (Red Hat 16.2.1-2) | 原生编译 |
| cmake | **4.3.0** | 构建系统 |
| ninja | 1.13.2 | CMake 生成器（可选） |
| make | 4.4.1 | 备用生成器 |
| gdb | 17.2 | 调试（**原生编译了 `arm-linux-gnu` 目标**，可直接调 ARM） |

### X11 开发文件

| 包 | 版本 |
|---|---|
| `libX11-devel` | 1.8.13 |
| `libxcb-devel` / `libXext-devel` / `xorg-x11-proto-devel` | 已装 |
| `pkgconf-pkg-config` | 2.5.1 |

### 一次性安装命令（Fedora）

```bash
sudo dnf install gcc gcc-c++ glibc-devel cmake ninja-build make gdb \
                 libX11-devel libxcb-devel libXext-devel xorg-x11-proto-devel \
                 pkgconf-pkg-config
```

**注意**：Fedora 上没有 `gcc-arm-linux-gnu`。本项目的 armhf 交叉编译走 Zig
（见第 3 节），不依赖发行版的交叉工具链包。

---

## 3. 交叉编译工具链

用的是 **Zig 0.16.0** 冒充交叉编译器，外面套一层 gcc 兼容的包装脚本。
**全部在用户级，不需要 root。**

| 组件 | 路径 |
|---|---|
| Zig 本体 | `~/.local/share/zig/zig`（稳定软链 → `zig-x86_64-linux-0.16.0/`） |
| glibc 动态目标 | `~/.local/bin/arm-linux-gnueabihf-{gcc,g++,cc,c++}` |
| musl 静态目标 | `~/.local/bin/arm-linux-musleabihf-{gcc,g++,cc,c++}` |
| 归档工具 | `~/.local/bin/arm-linux-gnueabihf-{ar,ranlib}` |
| ELF 分析 | `~/.local/bin/arm-linux-gnueabihf-{readelf,nm,size,strings}` |
| 包装脚本生成器 | `~/.local/share/gen-armhf-wrappers.sh` |
| 环境自检 | `~/.local/bin/armhf-env` |

包装脚本的内容形如：

```sh
#!/bin/sh
exec env LC_ALL=C "$HOME/.local/share/zig/zig" cc -target arm-linux-gnueabihf "$@"
```

**共 19 个包装脚本。** 自检：

```bash
armhf-env
```

### 工具链能力对照

| 能力 | Zig 自带？ | 说明 |
|---|---|---|
| glibc 头文件 + libc | ✅ 内置 | 交叉编译 glibc 程序开箱可用 |
| `linux/*.h`（内核 UAPI） | ✅ **611 个** | 嵌入式里常用的 `input.h`/`i2c-dev.h`/`spi/spidev.h` 等都有 |
| **`X11/*.h`（外部库）** | ❌ **一个都没有** | **需要第 4 节的 sysroot** |

---

## 4. X11 交叉编译环境（armhf-sysroot）

**位置：`~/Code/armhf-sysroot/`**（独立于本项目，属于工具链）

这是让 X11 GUI 能交叉编译的关键。Zig 不带任何 X11 头文件，所以从 Debian
拉了 armhf 的 libX11 开发包，解成一个 sysroot。

| 文件 | 作用 |
|---|---|
| `scripts/fetch-packages.py` | 从 Debian 源拉 16 个 armhf `.deb`（含依赖递归解析） |
| `scripts/make-sysroot.py` | 解包成 `sysroot/`（手工解 ar，Fedora 没有 `dpkg`） |
| `armhf-pkg-config` | pkg-config 包装：只查 sysroot，补 `-I`/`-L` 前缀 |
| `toolchain-armhf-x11.cmake` | CMake 工具链文件 |
| `build-armhf.sh` | 独立构建脚本（不走 CMake 也能编） |
| `cache/` | 下载的 `.deb`，约 15 MB |
| `sysroot/` | 解包结果，约 8.8 MB |

### 重建方法（换机器时需要）

```bash
cd ~/Code/armhf-sysroot
python3 scripts/fetch-packages.py    # 下载
python3 scripts/make-sysroot.py      # 解包
```

### 版本对齐很重要

拉的是 **Debian 12 bookworm** 的包，`libx11-6 = 2:1.8.4-2+deb12u2`
—— **和板子上跑的版本一致**，ABI 不会错配。

---

## 5. 目标板环境

**正点原子 ATK-DLMP135（STM32MP135）**，实测确认：

| 项目 | 值 |
|---|---|
| 硬件 | STM32MP135D-ATK Discovery Board |
| CPU | **Cortex-A7 单核 1 GHz**（ARMv7-A，32 位） |
| FPU | VFPv3 + **NEONv1**（工具链已默认启用） |
| ABI | `arm-linux-gnueabihf`（硬浮点） |
| 系统 | **Debian 12 bookworm，armv7l** |
| 内核 | 5.15.24（ST/正点原子 vendor 内核） |
| 内存 | **437 MiB 总量**，空闲约 324 MiB |
| 屏幕 | **1024×600**（正点原子 7 寸 RGB LCD 模块，型号 7016） |
| 触摸 | **Goodix Capacitive TouchScreen**，I2C-1 地址 0x14 |
| 桌面 | **Xorg `:0`，无窗口管理器**（`lightdm` 也没跑） |
| 包管理 | apt（已装约 577 个包，极简系统） |

### 板上需要装的开发包

```bash
sudo apt install g++ make pkg-config libx11-dev
```

排查触摸/显示时可加：

```bash
sudo apt install xinput x11-utils x11-apps
```

### 内存约束（437 MiB 是硬指标）

| 方案 | 常驻内存 |
|---|---|
| 裸 Xlib（本项目） | 约 2–4 MiB |
| 双缓冲（X Pixmap，1024×600） | +2.3 MiB |
| GTK4 空程序 | 约 40–80 MiB |

**这是本项目用裸 Xlib 而非 GTK/Qt 的量化理由。**

---

## 6. 构建与运行

### 6.1 用 `build.sh`（推荐）

```bash
./build.sh native      # 原生 x86-64: 配置 + 构建 + ctest
./build.sh run         # 原生构建后运行【控制台程序】(不开窗口)
./build.sh gui         # 原生构建后打开【GUI 窗口】(1024x600 窗口模式)
./build.sh gui-full    # 原生 GUI, 全屏
./build.sh armhf       # 交叉编译纯逻辑层 + 控制台程序 (无 GUI)
./build.sh gui-armhf   # 交叉编译 X11 GUI (需要 armhf-sysroot)
./build.sh all         # 原生 + armhf
./build.sh clean       # 删除构建目录
./build.sh verify      # 只做 armhf 产物结构验证
```

**最常见的困惑**：`./build.sh run` 跑的是**控制台程序，不开窗口**。
要看窗口用 `./build.sh gui`。

### 6.2 直接用 CMake

```bash
# 原生
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/halloworld-gui --windowed      # 窗口模式
./build/halloworld-gui                 # 全屏

# armhf 纯逻辑层（无 GUI）
cmake -B build-armhf -S . \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-armhf.cmake \
      -DBUILD_TESTING=OFF
cmake --build build-armhf -j

# armhf X11 GUI（需要 sysroot）
cmake -B build-gui-armhf -S . \
      -DCMAKE_TOOLCHAIN_FILE=$HOME/Code/armhf-sysroot/toolchain-armhf-x11.cmake \
      -DBUILD_TESTING=OFF -DWITH_GUI=ON
cmake --build build-gui-armhf -j
```

### 6.3 部署到板子

```bash
scp build-gui-armhf/halloworld-gui root@<板子IP>:~/
ssh root@<板子IP> 'DISPLAY=:0 ./halloworld-gui'
```

或者**板上原生编译**（更简单，不用 sysroot）：

```bash
scp src/gui.cpp root@<板子IP>:~/
ssh root@<板子IP> 'g++ -std=c++20 -O2 gui.cpp -o halloworld-gui $(pkg-config --cflags --libs x11)'
```

### 6.4 GUI 交互

窗口布局：

```
┌──────────────────────────────────────────────┬────────┐
│                                              │ 【退出】│  ← 右上角
│                                              └────────┘
│                                                        │
│                    halloworld                          │  ← 居中大标题
│                                                        │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐          │
│  │ 功能 1 │ │ 功能 2 │ │ 功能 3 │ │ 功能 4 │          │  ← 底部占位
│  └────────┘ └────────┘ └────────┘ └────────┘          │
└────────────────────────────────────────────────────────┘
```

| 操作 | 效果 |
|---|---|
| 点右上角 **【退出】** | 退出程序 |
| 点底部 **功能 1~4** | 目前只打印提示（**功能待定**） |
| 按 `q` / `Esc` | 退出程序 |
| 点窗口管理器关闭按钮 | 退出（板上没有 WM，板子上用不到，靠【退出】按钮） |

**注意**：**不支持"双击窗口退出"** —— 那是早期行为，已移除。原因见第 10.2 节 #12：
双击曾导致窗口"启动即关闭"的竞态，而且在触摸屏上容易误触。

按钮在 1024×600 上的实际尺寸（按窗口比例算，换屏幕不用改代码）：

| 按钮 | 位置 (x, y) | 尺寸 |
|---|---|---|
| 退出 | (933, 10) | 81×54 |
| 功能 1 | (10, 536) | 243×54 |
| 功能 2 | (263, 536) | 243×54 |
| 功能 3 | (516, 536) | 243×54 |
| 功能 4 | (769, 536) | 243×54 |

高度 54 px 是按触摸操作设计的（手指触点约 40–50 px），代码里限制在 36–64 px。

---

## 7. VS Code 配置

`.vscode/` 下三个文件：

| 文件 | 作用 |
|---|---|
| `tasks.json` | 10 个构建任务 |
| `settings.json` | 工作区设置 |
| `c_cpp_properties.json` | IntelliSense，指向 `compile_commands.json` |

**快捷键**：`Ctrl+Shift+B` = 原生全量构建。其余在 `Ctrl+Shift+P` → `Tasks: Run Task`。

GUI 相关的三个任务：

| 任务 | 说明 |
|---|---|
| `CMake: 构建 GUI 并运行 (原生预览)` | 编 x86-64 版并弹出 1024×600 窗口 |
| `CMake: 构建 GUI armhf (交叉编译)` | 编 ARM 版 |
| `CMake: 查看 armhf 产物架构` | 验证 ELF 头和 hard-float ABI |

**IntelliSense 说明**：`CMAKE_EXPORT_COMPILE_COMMANDS ON` 让 CMake 导出
`build/compile_commands.json`，`.vscode/c_cpp_properties.json` 指向它。所以
补全/跳转/报错与真实编译参数一致。
**前提是先跑一次配置**（`Ctrl+Shift+B` 会做），否则那个文件不存在。

---

## 8. 目录结构

```
FireControlApp/
├── README.md                      本文件
├── CMakeLists.txt                 顶层构建定义
├── build.sh                       便捷构建封装
├── .gitignore
├── cmake/
│   └── toolchain-armhf.cmake      armhf 工具链（纯逻辑层，无 X11）
├── src/
│   ├── greeting.h                 纯逻辑层接口
│   ├── greeting.cpp               纯逻辑层实现 → libfirecontrol.a
│   ├── main.cpp                   控制台程序入口
│   └── gui.cpp                    X11 图形界面
├── tests/
│   └── test_greeting.cpp          单元测试
└── .vscode/
    ├── tasks.json
    ├── settings.json
    └── c_cpp_properties.json
```

构建产物（不纳入版本控制）：

```
build/                原生 x86-64
build-armhf/          armhf 纯逻辑层 + 控制台
build-gui-armhf/      armhf X11 GUI
build-debug/          Debug 构建
```

---

## 9. 设计说明

### 9.1 逻辑层抽成静态库

`greeting.cpp` 编成 `libfirecontrol.a`，主程序和单元测试都链接它。好处：

- 逻辑代码被单元测试覆盖
- **能被交叉编译到 armhf**（不依赖任何平台特有库）

**加新源文件的注意事项**：CMake **不会自动扫描目录**。新增 `.cpp` 必须手动登记：

```cmake
add_library(firecontrol
    src/greeting.cpp
    src/你的新文件.cpp      # ← 必须加, 否则不会被编译
)
```

否则会出现"文件明明写了却没被编译"的困惑。

### 9.2 为什么用 Xlib + Xft，而不是 GTK/Qt

X11 是**客户-服务端**模型：应用程序只是 X 客户端，把绘图请求发给 X server。
所以客户端只需要库和协议头文件，不需要服务端那套。

| 方案 | 产物 | 依赖 | 437 MiB 板子 |
|---|---|---|---|
| **Xlib + Xft**（本项目） | 约 12 KB | libX11 + libXft + libfontconfig + libfreetype | ✅ |
| 纯 Xlib（无 Xft） | 约 8 KB | libX11 | ⚠️ **画不出中文** |
| GTK4 | 数 MB | glib/pango/cairo/… | ❌ 40–80 MiB 起步 |
| Qt6 | 数十 MB | 一大堆 | ❌ |

**为什么必须带 Xft**：X11 核心位图字体只有 **ISO-8859-1**（Latin-1）编码，
**没有汉字字形**。用 `XDrawString` 画中文，UTF-8 的多字节会被当成多个 Latin-1
字符各画一个，结果是乱码（实测显示成 `蚂蚁` 之类）。

Xft 走 **FreeType + fontconfig**，支持完整 Unicode 与 TrueType 反锯齿：

```cpp
XftFont *f = XftFontOpenName(dpy, scr, "WenQuanYi Zen Hei:size=18");
XftDrawString32(draw, &color, f, x, y, codepoints, n);
```

板子上已预装 **文泉驿正黑**（`wqy-zenhei.ttc`，含 Regular / Mono / 点阵三个变体），
`libXft.so.2` 和 `libfontconfig.so.1` 也都在（X 桌面环境自带）。

**实测验证**（本机 Xwayland + 文泉驿，用字形宽度指标判断真实渲染）：

| 文本 | UTF-8 字节 | 解析后码点 | 像素宽度 |
|---|---|---|---|
| `退出` | 6 | **2** ✓ | 48 |
| `功能 1` | 8 | **4** ✓ | 66 |
| `halloworld` | 10 | 10 | 113 |
| `中` | 3 | **1** ✓ | 24 |
| `A` | 1 | 1 | 14 |

汉字/字母宽度比 = **1.71** —— 汉字是宽字形，证明字形真实存在。
（若是乱码，2 个汉字会被拆成 6 个拉丁字符，码点数和宽度都会对不上。）

### 9.2.1 Xft 交叉编译的三个坑

| 现象 | 原因 | 解法 |
|---|---|---|
| `ft2build.h: file not found` | CMake 的 `FindX11` 只给 Xft 自己的头文件路径，**不给传递依赖**（freetype2 在 `/usr/include/freetype2`） | 改用 `pkg_check_modules(XFT xft x11)`，pkg-config 正确处理 `Requires` 链 |
| 链接报 `DSO missing from command line` | `xft.pc` 把 `x11` 放在 **`Requires.private`** 里，而 `pkg-config --libs` **默认不输出私有依赖** | 显式写 `pkg_check_modules(XFT QUIET xft x11)` |
| `--cflags xft` 没有 `-I/usr/include` | pkg-config 认为 `/usr/include` 是默认路径会省略，但交叉编译时 zig 不把 sysroot 的 include 当默认 | toolchain 里显式加 `-isystem $SYSROOT/usr/include` |

### 9.3 触摸在 X11 里就是普通 Button 事件

```
Goodix 触摸面板 ──I2C──▶ 内核 goodix 驱动 ──▶ /dev/input/event0
                                                      │
                                          X server (libinput/evdev)
                                                      │
                            应用收到 ButtonPress / MotionNotify
```

**单点触摸在 X11 里就是普通的 `ButtonPress`，坐标在 `xbutton.x/y`。**
所以裸 Xlib 就能收触摸，**不需要 XInput2**（XI2 的价值在多指手势和压力值）。

### 9.4 全屏而非窗口模式

板上**没有窗口管理器**，普通模式下窗口位置和尺寸没人管。所以：

- 默认全屏，自己用 `XMoveResizeWindow` 占满屏幕
- 窗口没有标题栏、边框、关闭按钮
- `--windowed` 参数是给开发机预览用的

---

## 10. 已知问题与踩坑记录

按"踩到的坑"和"当前遗留问题"分开。

### 10.1 ⚠️ 当前遗留 / 需要注意

| # | 问题 | 说明 |
|---|---|---|
| 1 | **GUI 产物是动态链接的** | 运行时依赖板子上的 `libX11.so.6`。**已在板上实测通过** —— 板子跑着 Xorg，该库存在，动态链接正常 |
| 2 | **`halloworld` 拼写** | `hallow` 通常是 `hollow` 的笔误。沿用最初文件名未改 |
| 3 | **Window 首次映射可能收到杂散事件** | 见 10.2 #12。已缓解：忽略映射后 400 ms 内的点击，且退出改为点按钮（不再是单击/双击全屏）。根因（X server/WM 行为）不在本项目控制内 |
| 4 | **armhf 的 GUI 无法在开发机运行** | 架构不同 + 需要板子的 libX11。必须 scp 到板子 |
| 5 | ~~X11 中文显示~~ **已解决** | 原方案用 X11 核心位图字体（只有 ISO-8859-1，画不出汉字，中文显示为乱码）。**已改用 Xft + FreeType + fontconfig**，中文正常。见第 9.2 节 |
| 6 | **CMake 不自动扫描源文件** | 新增 `.cpp` 必须手动加到 `CMakeLists.txt`，见 9.1 |
| 7 | **`build.sh run` 不开窗口** | 它跑的是控制台程序。看窗口用 `build.sh gui` |
| 8 | **armhf 首次编译慢** | Zig 要从源码构建 libc++，首次约 65 秒。之后走缓存几秒 |

### 10.2 踩过的坑（都已解决，记录以备重犯）

#### Zig 相关

| # | 现象 | 原因 | 解法 |
|---|---|---|---|
| 1 | CMake configure 直接失败，报 `error: FileNotFound` | **Zig 0.16 的 `-fsyntax-only` 是坏的**，而 CMake 探测编译器正是用它 | toolchain 里加 `set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)`。**这是 CMake + Zig 交叉编译能跑通的关键** |
| 2 | `libc of the specified target requires dynamic linking` | **glibc 目标不支持 `-static`** | 静态链接必须换 musl（`arm-linux-musleabihf-*`） |
| 3 | 无法事后 strip ARM 文件 | Fedora 的 binutils 只编了 x86 BFD 后端；`zig objcopy` 报 `unimplemented` | **链接期加 `-s`**。CMake 里 `target_link_options(... PRIVATE -s)`。效果：5.7 MB → 387 KB |
| 4 | `__DATE__`/`__TIME__` 在 zig 下报**错误**（不是警告） | Zig 的 clang 默认把 `-Wdate-time` 当 error（不可复现构建） | 代码里不用这两个宏。同一个 `-Wall -Wextra`，**gcc 只警告、clang 报错** |
| 5 | `XSetWMProtocols` 编译报错：`const Atom*` 无法转成 `Atom*` | **gcc 容忍 const 不匹配，clang 直接报错** | 去掉 `const`。又是 GCC 与 Clang 的差异 |
| 6 | 119 条 nullability 警告 | Zig 从源码构建 libc++，那些头文件的警告 | 加 `-Wno-nullability-completeness` |
| 7 | `zig ar`/`ranlib` 不接受 `-target` | Zig 0.16 的限制 | 包装脚本里不加该参数 |
| 8 | Zig 0.16 没有 `readelf`/`nm`/`size`/`strings`/`ld`/`as` 子命令 | 移除 | ELF 分析改用主机 GNU binutils（`readelf` 读 ELF 头是架构无关的） |

#### X11 交叉编译相关

| # | 现象 | 原因 | 解法 |
|---|---|---|---|
| 9 | `X11/Xlib.h: file not found` | **Zig 一个 X11 头文件都不带**（X11 是外部库，不是内核 UAPI） | 建 ARM sysroot，见第 4 节 |
| 10 | CMake 报 `Could NOT find X11 (missing: X11_X11_LIB)` | `find_library` 只找 `/usr/lib`，而 ARM 库在 `/usr/lib/arm-linux-gnueabihf/`。**只设 `CMAKE_FIND_ROOT_PATH` 不够** —— 它只给已知搜索路径加前缀，不会自己猜出 Debian 的三元组目录 | toolchain 里显式加 `CMAKE_LIBRARY_PATH` / `CMAKE_INCLUDE_PATH` |
| 11 | pkg-config 的 `-I` 输出为空 | `/usr/include` 是默认路径，pkg-config 会省略 | toolchain 里用 `-isystem` 显式补。另外 **Fedora 的 pkg-config 没有 Debian 的 `PKG_CONFIG_SYSROOT_DIR` 前缀机制**，要自己写包装脚本 |
| 12 | **窗口有约 1/3 概率自动关闭** | 窗口刚映射时收到启动瞬间遗留的**杂散 `ButtonPress`**；当时"点一下退出"就被它关掉。间歇性的，很难查 | 忽略映射后 400 ms 内的点击。后来退出改为**点右上角按钮**，点击不再有退出语义，风险进一步降低 |

#### 环境 / 工具相关

| # | 现象 | 原因 | 解法 |
|---|---|---|---|
| 13 | `readelf` 输出解析出空值 | **字段名被 locale 本地化**（中文下 `Machine` → 「机器」） | 所有解析 ELF 输出的脚本强制 `LC_ALL=C` |
| 14 | 下载的 Debian 索引损坏，`EOFError` | 下载被中途打断，留下残缺 gzip 被当成缓存 | 脚本先下到 `.part`，校验通过才改名 |
| 15 | `#include <linux/...>` 交叉编译能用，`<X11/...>` 不能 | `linux/*` 是内核 UAPI（Zig 自带 611 个）；`X11/*` 是外部库 | 前者直接用，后者需要 sysroot |
| 16 | 注释行导致 `-Wcomment` 警告 | `//` 注释**以反斜杠结尾会把下一行也吞进注释**（行拼接） | 注释里别用行尾反斜杠 |

### 10.3 其他可能的坑（未遇到但值得知道）

| 现象 | 原因 | 处理 |
|---|---|---|
| 触摸坐标偏移 / X 轴镜像 | 触摸控制器坐标范围与屏幕分辨率不匹配 | `xinput set-prop <id> "Coordinate Transformation Matrix" ...` |
| 屏幕不亮 | 背光 GPIO（PD13 / `LCD_BL`）没使能 | 查设备树 `backlight` 节点 |
| 触摸内核认到但 X 不认 | X server 没加载 evdev/libinput 驱动 | 装 `xserver-xorg-input-libinput`，查 `/var/log/Xorg.0.log` |
| `xdpyinfo: unable to open display ""` | ssh/串口会话里 `DISPLAY` 为空；Xorg 还带 `-auth` | `export DISPLAY=:0`，必要时设 `XAUTHORITY` |
| 系统里没有 `gcc-arm-linux-gnu` | Fedora 不提供这个包名 | 本项目用 Zig，不需要它 |

---

## 11. 实测结果

### 开发机（x86-64，Fedora 44）

| 项目 | 结果 |
|---|---|
| 原生 configure | GCC 16.2.1，C++20，`IS_CROSS_BUILD=OFF`，`HAVE_GUI=TRUE` |
| 原生构建 | **0 warning** |
| `ctest` | **1/1 passed** |
| `halloworld` | 13,568 字节，输出 `hallo world` |
| `halloworld-gui` | 18,040 字节，1024×600 窗口正常显示，**6/6 稳定驻留** |
| `test_greeting` | 13,624 字节，全部断言通过 |
| `compile_commands.json` | 3 个编译单元，含真实编译标志 |

### armhf 交叉编译

| 项目 | 结果 |
|---|---|
| 纯逻辑层 configure | Clang 21.1.0 (Zig)，`CMAKE_SYSTEM_PROCESSOR=arm` |
| 纯逻辑层构建 | `ELF32 ARM hard-float ABI`，musl 静态链接，**约 387 KB** |
| X11 GUI 构建 | `ELF32 ARM hard-float ABI`，**8,168 字节**，动态链接 |
| GUI 动态依赖 | `libX11.so.6` / `libc.so.6` |
| 目标文件对照 | 原生 `x86-64` / armhf `ARM`（确认没被误编） |
| 首次 configure 耗时 | 约 65 秒（Zig 构建 libc++），之后走缓存 |

### ✅ 已在板上实测通过（关键里程碑）

**交叉编译的 ARM 二进制已在真实硬件（STM32MP135）上成功运行**，
全屏窗口正常显示 `halloworld`。这一次运行同时验证了几件此前只是"推断"的事：

| 验证项 | 之前的状态 | 现在 |
|---|---|---|
| 交叉编译产物能在 Cortex-A7 上执行 | 只验了 ELF 结构 | ✅ 实际运行成功 |
| 动态链接的 libX11 能对上板子版本 | 只对比了版本号 | ✅ 链接并运行正常 |
| 硬浮点 ABI 与板子兼容 | 只看了 ELF Flags | ✅ 正常 |
| 无窗口管理器时全屏模式可用 | 只是设计推断 | ✅ 全屏铺满显示 |
| `XDrawString` + 位图字体能出字 | 未验 | ✅ 文字正常显示 |

### ⚠️ 仍未验证的部分

- **触摸屏的实际事件**：点【退出】按钮能否退出、按钮命中区域是否准确、坐标是否对齐，都还没在板上试过
- **窗口模式**（`--windowed`）在板上无 WM 环境下的表现
- 长时间运行的稳定性
- 实际触摸坐标范围与 1024×600 是否匹配

---

## 12. 待办

- [x] ~~在板子上实测 `halloworld-gui`~~ —— **已完成，全屏窗口正常显示**
- [ ] 板上实测触摸：点【退出】按钮、点占位按钮的命中区域是否准确，坐标是否偏移
- [ ] 确认全屏下 34 号字的观感是否合适（偏大偏小都可调）
- [ ] 若坐标偏移，写 `Coordinate Transformation Matrix` 校准
- [ ] 决定是否需要一个极简窗口管理器（`matchbox-window-manager`、`openbox`），
      否则所有程序都只能全屏
- [x] ~~引入 Xft + FreeType 让中文正常显示~~ —— **已完成**，板上实测字体加载成功
- [ ] 给 `src/` 加自动源文件扫描（`file(GLOB ...)`），免得每次加文件都要改 CMakeLists
