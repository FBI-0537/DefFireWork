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

> ### 合作声明
>
> 本项目由两人共同开发，各自负责的模块如下：
>
> | 成员 | 分工 |
> |---|---|
> | **Skywindfox** | 硬件验证与系统环境（开发板、Debian 系统、Fedora + Zig 交叉编译）、CMake 工程与中控软件、文档 |
> | **FBI-0537** | 交叉编译环境（`armhf-toolchain/`，Windows + Docker）与构建脚本、LVGL 界面重构 |
>
> **约定**：各自的编译环境与原有注释都保留，改动对方负责的文件时不删他的说明；
> 两个开发机上的交叉编译产物互不替代（Fedora + Zig 与 Windows + Docker 各成一套）。

- 本项目旨在完成省级大创项目（由两人共同合作）的目的，本仓库仅作个人存储，不建议对外使用
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

一个 CMake 工程，产出三个可执行文件和两个静态库：

| 目标 | 源文件 | 说明 | 能否交叉编译 |
|---|---|---|---|
| `halloworld` | `src/main.cpp` | **控制台程序**，打印 `hallo world`。不开窗口 | ✅ 能 |
| `halloworld-gui` | `src/gui.cpp` | **LVGL 图形界面**（X11 后端），全屏/窗口显示 `halloworld` | ✅ 能（需 ARM 版 X11 + freetype，见第 4 节） |
| `test_greeting` | `tests/test_greeting.cpp` | 单元测试，`ctest` 调用 | ❌ 交叉产物跑不了 |
| `libfirecontrol.a` | `src/greeting.cpp` | 纯逻辑层静态库，上面两个程序都链接它 | ✅ 能 |
| `liblvgl.a` | `third_party/lvgl/` | LVGL v9.2.3 源码（随仓库提交），只被 `halloworld-gui` 使用 | ✅ 能 |

界面用的是 **LVGL v9.2.3**：控件、布局、字体、事件都由它管，自己写的不再是
"画矩形 + 命中检测"，而是控件树 + 回调。配置在 `lv_conf.h`（只写覆盖项，其余走
LVGL 默认值），显示后端选的是 LVGL 自带的 X11 驱动，中文由 FreeType 在运行时从
板上的文泉驿正黑取字形。

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

### 板上需要的运行时依赖

GUI 是动态链接的，跑之前确认这三样都在（板子都自带，一般不用管）：

| 依赖 | 用途 | 板上来源 |
|---|---|---|
| `libX11.so.6` | LVGL 的 X11 显示/输入后端 | Xorg |
| `libfreetype.so.6` | LVGL 的中文字形 | Xft 的传递依赖 |
| 中文字体（文泉驿正黑 `wqy-zenhei.ttc`） | 界面中文 | `fonts-wqy-zenhei` |

缺字体时会打印警告（汉字变方框），可以用参数指定别的字体文件绕开：

```bash
./halloworld-gui --font=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
# 或者用环境变量, 调试时省得每次敲参数
FIRECONTROL_FONT=/path/to/font.ttc ./halloworld-gui
```

### 内存约束（437 MiB 是硬指标）

LVGL 这条路的显示开销（**按源码算出来的，不是板上实测值**）：

| 项 | 大小 | 出处 |
|---|---|---|
| XImage 缓存（整屏 32bpp） | 1024×600×4 ≈ **2.4 MiB** | `lv_x11_display.c` |
| 绘制缓冲（局部刷新 = 屏的 1/10，双缓冲） | 2 × 246 KiB ≈ **0.5 MiB** | 同上 |
| 中文字形缓存 | 按需增长（上限 256 字形） | `lv_conf.h` |

总计 **3 MiB 量级**，对比：

| 方案 | 常驻内存 |
|---|---|
| LVGL + X11 后端（本项目） | 显示路径约 3 MiB（估算） |
| GTK4 空程序 | 约 40–80 MiB |
| Qt6 | 数十 MiB 起 |

**这是本项目继续留在轻量框架而不是转 GTK/Qt 的量化理由。**

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

**在 Windows 上**走 Docker 那套，不用 `build.sh`：

```powershell
cd armhf-toolchain
.\build-armhf.ps1                    # 默认: 交叉编译 GUI + 产物校验
.\build-armhf.ps1 -Action console    # 只编控制台程序与逻辑库 (不含 GUI, 快)
.\build-armhf.ps1 -Action shell      # 进容器手动操作
```

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

# armhf GUI（需要带 ARM 版 X11/freetype 的交叉环境）
# 推荐直接用 armhf-toolchain/build-armhf.ps1（Docker）。它内部做的事等价于:
cmake -B /out -S . \
      -DCMAKE_TOOLCHAIN_FILE=/tc/toolchain-armhf-x11.cmake \
      -DBUILD_TESTING=OFF -DWITH_GUI=ON
cmake --build /out -j
# 容器里: /work = 仓库(只读)、/tc = 工具链、/out = ../armhf-toolchain/Output
```

GUI 相关的可调项（都是 cache 变量）：

| 变量 | 默认 | 说明 |
|---|---|---|
| `WITH_GUI` | `AUTO` | `AUTO` 时：原生有 x11/freetype 就编，交叉编译直接跳过 |
| `LVGL_DIR` | `third_party/lvgl` | LVGL 源码目录 |
| `LV_CONF_PATH` | `<仓库>/lv_conf.h` | LVGL 配置文件。**在 `add_subdirectory` 之前设置**才有效 |

### 6.3 部署到板子

```bash
scp build-gui-armhf/halloworld-gui root@<板子IP>:~/     # Fedora + Zig 那条路线
ssh root@<板子IP> 'DISPLAY=:0 ./halloworld-gui'
```

走 Windows + Docker 那套的话，产物在 `armhf-toolchain/Output/`（不落在本仓库里）：

```powershell
scp "..\armhf-toolchain\Output\halloworld-gui" root@<板子IP>:~/
```

> **板上原生编译这条路现在走不通了**：以前 `gui.cpp` 是单文件、只用 Xft，
> `g++ gui.cpp $(pkg-config --libs xft x11)` 就行。现在要用 LVGL，得把整个
> `third_party/lvgl` + `lv_conf.h` 拷过去，还要装 `libfreetype-dev`、
> 编 311 个 `.c` —— 不划算。**直接交叉编译再 scp 过去。**

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
| 点底部 **功能 1~4** | 目前只打印提示（**功能待定**），顺带打印点击坐标 |
| 按 `q` / `Esc` | 退出程序 |
| 点窗口管理器关闭按钮 | 退出（板上没有 WM，板子上用不到，靠【退出】按钮） |

打印坐标是故意留的：板上还没验证过触摸坐标是否偏移（见第 12 节待办），
点一下就能看出来。

**实现上的几个点**：

- **布局**：主标题居中；退出按钮贴右上角；底部 4 个按钮放在一个 flex 行里，
  每个 `flex_grow=1` 自动等宽，列间距 = 屏宽的 1%。换屏幕尺寸不用改代码。
- **窗口尺寸**只在启动时算一次。板上没有窗口管理器，没人会去缩放它 ——
  `--windowed` 只是给开发机预览用的 1024×600。
- **按钮文字**用 FreeType 装 18 px，主标题 40 px，字体文件在启动时按候选表找
  （见第 5 节）。
- **`q` / `Esc` 为什么能全局生效**：LVGL 的键盘输入只发给"输入组里当前被聚焦的
  对象"，组里没有聚焦对象时按键会被直接丢掉。所以启动时把**屏幕本身**加进默认
  组并聚焦它（按钮不进组，点按钮不会把焦点抢走）。
- **启动后 400 ms 内的点击会被忽略**：窗口刚映射时可能收到遗留的杂散点击，
  见第 10.2 节 #12。
- **窗口模式下会画一个跟随鼠标的小指针**：LVGL 的 X11 后端会把 X 光标隐藏掉，
  板上是触摸屏不需要，但开发机预览时不画个指针就不知道鼠标在哪。

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
底部一排的宽度由 flex 均分得出（(1024−20−3×10)/4 = 243.5），整数取整后
可能有 ±1 像素的差异 —— 位置表里的 x 是按 243 算的。

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
交叉编译时导出的是 armhf 那套参数（`build-armhf/compile_commands.json`），
里面也包含 LVGL 的头文件路径 —— 改 GUI 代码时用它补全更准。

> ⚠️ 这些任务都用 `bash`，是针对 Linux 开发机写的。在 Windows 上构建走
> `armhf-toolchain/build-armhf.ps1`（Docker），任务列表还没跟着改。

---

## 8. 目录结构

```
FireControlApp/
├── README.md                      本文件
├── CMakeLists.txt                 顶层构建定义
├── lv_conf.h                      LVGL 配置（只写覆盖项）
├── build.sh                       便捷构建封装（Linux 开发机）
├── .gitignore
├── cmake/
│   └── toolchain-armhf.cmake      armhf 工具链（纯逻辑层，无 X11）
├── src/
│   ├── greeting.h                 纯逻辑层接口
│   ├── greeting.cpp               纯逻辑层实现 → libfirecontrol.a
│   ├── main.cpp                   控制台程序入口
│   └── gui.cpp                    LVGL 图形界面（X11 后端）→ halloworld-gui
├── third_party/
│   └── lvgl/                      LVGL v9.2.3 源码（已裁掉 docs/demos/tests/examples）
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

> 上面这几个都在本仓库里，由 `build.sh`（Fedora + Zig 那条路线）产生。
>
> **Windows + Docker 那条路线（`armhf-toolchain/`）的产物不在本仓库**，
> 统一落在 `../armhf-toolchain/Output/`：源码目录是**只读挂载**，构建写不进来。
> 两条路线的产物目录互不替代、互不覆盖 —— 改对方那条路线时不要动这些目录。

> LVGL 源码是**直接放进仓库**的（不是 submodule，也不是 configure 时下载），
> 所以任何机器上拉下来、断网也能编。为了让仓库不至于被撑大，裁掉了只跟
> 文档/示例/测试/CI 有关的目录：`docs/` `demos/` `examples/` `tests/` `scripts/`
> `.github/` `.devcontainer/`，以及 `env_support/cmsis-pack/`（5.6 MB 的 Keil/MDK
> 打包件，本项目用不到）。裁完 16.1 MB / 827 个文件。
> **升级 LVGL 时要保持同样的裁剪**，替换后重新跑一次构建确认。

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

### 9.2 为什么用 LVGL，后端为什么选 X11 + FreeType

第一阶段是手写 Xlib + Xft：自己画矩形、自己算坐标、自己做命中检测。能跑，
但**再加一个界面元素就要把"布局 + 绘制 + 命中检测"重写一遍**，继续堆下去不合适。
换成 LVGL 之后这些交给控件树的样式/布局系统，界面代码只剩"建控件 + 挂回调"。

**LVGL 的代价**（437 MiB 板子上的硬指标）：

| 项 | 值 |
|---|---|
| 源码（裁剪后，随仓库提交） | 21.7 MB |
| `halloworld-gui` 二进制（armhf, stripped） | **399,624 字节** |
| 显示路径运行时内存 | 约 3 MiB（算法见第 5 节） |
| 运行时依赖 | `libX11.so.6` + `libfreetype.so.6`（板上都有） |

#### 显示后端为什么不用 framebuffer

LVGL 更"嵌入式"的用法是 `LV_USE_LINUX_FBDEV` 直接写 `/dev/fb0`
（+ `LV_USE_EVDEV` 读触摸）。但**板上跑着 Xorg，`/dev/fb0` 归它管** ——
两个程序抢同一块屏，画出来的东西会互相覆盖。选 X11 后端的好处是
**部署方式一行都不用改**，而且触摸在 X11 里就是普通鼠标事件（见 9.3）。

真要去掉 Xorg，只需要把 `gui.cpp` 里的显示初始化换掉：

```cpp
// 现在（X11 后端）
g_disp = lv_x11_window_create(kTitle, win_w, win_h);
lv_x11_inputs_create(g_disp, nullptr);

// 换成 framebuffer + evdev（界面代码一行不用动）
g_disp = lv_linux_fbdev_create();
lv_linux_fbdev_set_file(g_disp, "/dev/fb0");
lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0");
```

#### 中文为什么走 FreeType，而不是预生成字库

`lv_font_conv` 把用到的汉字转成 C 数组编进程序：零运行时依赖、启动最快，
但**每改一个字都要重新生成**，而且字库体积随字数增长。
开 `LV_USE_FREETYPE` 则是运行时从 `wqy-zenhei.ttc` 取字形 —— 改文案不用重编，
界面里出现任何汉字都能显示。代价是多一个 `libfreetype` 依赖，而板上本来就有
（Xft 也在用它）。

#### `lv_conf.h` 改了哪几处

只覆盖 4 处，其余全部走 LVGL 的默认值（`src/lv_conf_internal.h` 会补）：

| 配置 | 值 | 为什么 |
|---|---|---|
| `LV_COLOR_DEPTH` | 32 | X11 后端内部本来就按 XRGB8888 组 XImage，32 位是**零转换路径**；16 位每个像素都要做一次 RGB565→RGB888 展开 |
| `LV_USE_STDLIB_*` | `LV_STDLIB_CLIB` | 用 libc 堆。内置内存池要在编译期定死 `LV_MEM_SIZE`，而中文字形缓存的峰值取决于用户点开什么界面，给不准 |
| `LV_USE_X11` | 1 | 显示 + 输入后端（含键盘/滚轮） |
| `LV_USE_FREETYPE` | 1 | 中文字形 |

### 9.2.1 集成 LVGL 踩到的三个坑

| 现象 | 原因 | 解法 |
|---|---|---|
| 编译时满屏 `#pragma message: Possible failure to include lv_conf.h` | LVGL 用 `LV_CONF_H` 这个宏名判断配置文件到底有没有被包含；自己的 `lv_conf.h` 顺手写了 `FIRECONTROL_LV_CONF_H` 当防重宏，LVGL 不认 | 防重宏必须叫 `LV_CONF_H` |
| `fatal error: ft2build.h: No such file or directory`，报错行落在 `lv_draw_vg_lite_label.c` | `ft2build.h` 在 `/usr/include/freetype2`，不是默认搜索路径。**只给 `halloworld-gui` 加 include 路径不够** —— LVGL 是独立目标，它编 `lv_freetype.c` 时也要这个路径 | 把 `GUI_DEPS_INCLUDE_DIRS` 同时挂到 `lvgl` 目标上 |
| 按 `q` / `Esc` 完全没反应 | LVGL 的键盘事件只发给"输入组里当前被聚焦的对象"；组里没有聚焦对象时按键会被**直接丢掉** | 启动时把屏幕本身加进默认组，再 `lv_group_focus_obj(屏幕)` |

### 9.3 触摸在 X11 里就是普通 Button 事件

```
Goodix 触摸面板 ──I2C──▶ 内核 goodix 驱动 ──▶ /dev/input/event0
                                                      │
                                          X server (libinput/evdev)
                                                      │
                            应用收到 ButtonPress / MotionNotify
```

**单点触摸在 X11 里就是普通的 `ButtonPress`，坐标在 `xbutton.x/y`。**
所以不需要 XInput2（XI2 的价值在多指手势和压力值）。LVGL 的 X11 输入驱动
就是把 `ButtonPress` 翻译成 `LV_INDEV_TYPE_POINTER` 的按下/抬起。

### 9.4 全屏而非窗口模式

板上**没有窗口管理器**，普通模式下窗口位置和尺寸没人管。所以：

- 默认按 X 报告的屏幕实际尺寸建窗口（`XOpenDisplay` 查一次，查完就关），
  位置 (0,0)、无边框 —— 没有 WM 时 X 会把它放在请求的位置并拉高
- 窗口没有标题栏、边框、关闭按钮
- `--windowed` 参数固定 1024×600，是给开发机预览用的
- 布局只在启动时算一次：没有 WM 就没人会去缩放窗口

---

## 10. 已知问题与踩坑记录

按"踩到的坑"和"当前遗留问题"分开。

### 10.1 ⚠️ 当前遗留 / 需要注意

| # | 问题 | 说明 |
|---|---|---|
| 1 | **LVGL 版还没在板上实测过** | 只做了交叉编译验证（构建 0 警告、产物是 armv7 hard-float、`NEEDED` 为 libX11/libfreetype）。**上手先点一遍【退出】和底部按钮**，确认触摸坐标、中文显示、字体路径都对 |
| 2 | **GUI 产物是动态链接的** | 运行时依赖板子上的 `libX11.so.6` 和 `libfreetype.so.6`。老版（Xft）已在板上实测通过；新版依赖的这两个库板上都有，但没实测 |
| 3 | **`halloworld` 拼写** | `hallow` 通常是 `hollow` 的笔误。沿用最初文件名未改 |
| 4 | **Window 首次映射可能收到杂散事件** | 见 10.2 #12。已缓解：忽略启动后 400 ms 内的点击，且退出改为点按钮。根因（X server/WM 行为）不在本项目控制内 |
| 5 | **armhf 的 GUI 无法在开发机运行** | 架构不同 + 需要板子的 libX11。必须 scp 到板子（或在容器里用 qemu + Xvfb 跑，但那要额外装包） |
| 6 | **CMake 不自动扫描源文件** | 新增 `.cpp` 必须手动加到 `CMakeLists.txt`，见 9.1 |
| 7 | **`build.sh run` 不开窗口** | 它跑的是控制台程序。看窗口用 `build.sh gui` |
| 8 | **armhf 首次编译慢** | Zig 要从源码构建 libc++（约 65 秒）；LVGL 有 311 个 `.c`，Docker 里首次全量编译要几分钟，之后增量 |
| 9 | **容器里 apt 源可能不通** | 镜像用的是清华 TUNA。网络受限时 `docker build` 会在装包那步失败，此时可用已有的旧镜像继续编（多装了 libxft 之类，不影响） |
| 10 | **LVGL 源码是手工裁剪过的** | 见第 8 节。升级 LVGL 时按同样的目录裁剪 |

### 10.2 踩过的坑（都已解决，记录以备重犯）

（LVGL 集成踩的三个坑记在 **9.2.1** 节，这里不重复。）

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

### 11.1 LVGL 版（当前）

#### 开发机（Windows + Docker）

| 项目 | 结果 |
|---|---|
| armhf configure | 容器 Debian 12 bookworm，`arm-linux-gnueabihf-g++` **GNU 12.2.0**，`IS_CROSS_BUILD=ON`，`HAVE_GUI=TRUE` |
| armhf 构建 | **0 error**；唯一一条 warning 是 `src/greeting.cpp` 里那行测试代码的 `unused variable 'a'`（见 armhf-toolchain README 5.3），与本轮改动无关 |
| 原生 x86-64 构建 / `ctest` | ⚠️ **未做** —— 这台机器是 Windows，没有 x86-64 Linux 环境 |

#### armhf 交叉编译产物（实测，位于 `armhf-toolchain/Output/`）

| 产物 | 结果 |
|---|---|
| `halloworld` | ELF32 ARM hard-float，stripped，**5,660 字节** |
| `halloworld-gui` | ELF32 ARM hard-float，stripped，**399,624 字节** |
| GUI 动态依赖 | `libX11.so.6` / `libfreetype.so.6` / `libgcc_s.so.1` / `libc.so.6` |
| `libfirecontrol.a` / `liblvgl.a` | 静态库 |
| LVGL 编译单元 | 311 个 `.c`；`lv_freetype*.o`、`lv_x11*.o` 都在（说明 `lv_conf.h` 生效了） |

#### 运行验证

| 项目 | 结果 |
|---|---|
| ARM 二进制能在本机装载执行（Docker Desktop 的 binfmt/qemu） | ✅ |
| `halloworld-gui` 连不上 X server 时的报错与排查提示 | ✅ 实测输出正常 |
| **真实 X server 下的界面**（窗口、中文、点击、按键） | ⚠️ **未验证** —— 环境里 apt 源被网络策略挡了（502），装不了 `qemu-user-static` / `Xvfb`。要在板上试，或者在带 X 的 Linux 上编原生版试 |

### 11.2 老版（Xlib + Xft）的实测记录

以下是**重构之前那一版**的成绩，保留作历史证据 —— "ARM 产物能执行、动态链接、
无 WM 全屏、中文能显示"这几件事在板上是跑通过的。

#### 开发机（x86-64，Fedora 44）

| 项目 | 结果 |
|---|---|
| 原生 configure | GCC 16.2.1，C++20，`IS_CROSS_BUILD=OFF`，`HAVE_GUI=TRUE` |
| 原生构建 | **0 warning** |
| `ctest` | **1/1 passed** |
| `halloworld` | 13,568 字节，输出 `hallo world` |
| `halloworld-gui` | 18,040 字节，1024×600 窗口正常显示，**6/6 稳定驻留** |
| `test_greeting` | 13,624 字节，全部断言通过 |
| `compile_commands.json` | 3 个编译单元，含真实编译标志 |

#### armhf 交叉编译（Fedora + Zig）

| 项目 | 结果 |
|---|---|
| 纯逻辑层 configure | Clang 21.1.0 (Zig)，`CMAKE_SYSTEM_PROCESSOR=arm` |
| 纯逻辑层构建 | `ELF32 ARM hard-float ABI`，musl 静态链接，**约 387 KB** |
| X11 GUI 构建 | `ELF32 ARM hard-float ABI`，**8,168 字节**，动态链接 |
| GUI 动态依赖 | `libX11.so.6` / `libc.so.6` |
| 目标文件对照 | 原生 `x86-64` / armhf `ARM`（确认没被误编） |
| 首次 configure 耗时 | 约 65 秒（Zig 构建 libc++），之后走缓存 |

#### 板上实测通过（关键里程碑）

**交叉编译的 ARM 二进制已在真实硬件（STM32MP135）上成功运行**，
全屏窗口正常显示 `halloworld`。这一次运行同时验证了几件此前只是"推断"的事：

| 验证项 | 之前的状态 | 现在 |
|---|---|---|
| 交叉编译产物能在 Cortex-A7 上执行 | 只验了 ELF 结构 | ✅ 实际运行成功 |
| 动态链接的 libX11 能对上板子版本 | 只对比了版本号 | ✅ 链接并运行正常 |
| 硬浮点 ABI 与板子兼容 | 只看了 ELF Flags | ✅ 正常 |
| 无窗口管理器时全屏模式可用 | 只是设计推断 | ✅ 全屏铺满显示 |
| `XDrawString` + 位图字体能出字 | 未验 | ✅ 文字正常显示 |

⚠️ 但这些**不能直接套到 LVGL 版上**：显示/输入路径、字体加载方式都换了，
新版必须重新在板上跑一遍。

### 11.3 ⚠️ 仍未验证的部分（LVGL 版）

- **触摸屏的实际事件**：点【退出】/底部按钮能否触发、命中区域与坐标是否对齐
- **中文显示**：LVGL 的 FreeType 后端能否在板上找到并加载 `wqy-zenhei.ttc`
- **窗口模式**（`--windowed`）在无 WM 环境下的表现
- 长时间运行的稳定性、实际常驻内存

---

## 12. 待办

- [ ] **在板上实测 LVGL 版**：全屏显示是否正常、中文是否正常、点【退出】能否退出
- [ ] 板上实测触摸：点【退出】按钮、点占位按钮的命中区域是否准确，坐标是否偏移
      （占位按钮的回调会打印点击坐标，直接对照屏幕看）
- [ ] 确认全屏下 40 号标题 / 18 号按钮字的观感是否合适（`gui.cpp` 里的
      `kBigFontPx` / `kSmallFontPx`，或换成 34 号试试）
- [ ] 若坐标偏移，写 `Coordinate Transformation Matrix` 校准
- [ ] 决定是否需要一个极简窗口管理器（`matchbox-window-manager`、`openbox`），
      否则所有程序都只能全屏
- [ ] 量一下 LVGL 版的真实常驻内存，把第 5 节的估算值换成实测值
- [ ] 给 `src/` 加自动源文件扫描（`file(GLOB ...)`），免得每次加文件都要改 CMakeLists
- [ ] 有网时 `docker build -t firecontrol-armhf:bookworm .` 重建镜像
      （Dockerfile 已去掉不再需要的 libxft/libfontconfig）；用旧镜像也能编
