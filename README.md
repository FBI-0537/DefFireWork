# FireControlApp

## 致谢

- [@Skywindfox](https://github.com/Skywindfox) —— 项目发起、硬件平台搭建、系统环境、CMake 工程与中控软件
- [@FBI-0537](https://github.com/FBI-0537) —— 交叉编译环境与构建脚本、LVGL 界面重构

依赖的开源项目：Zig、X.Org (Xlib/Xft)、FreeType、fontconfig、[LVGL](https://lvgl.io/)、CMake，
以及中文字体[文泉驿](http://wenq.org/)（板上）和 [Noto Sans CJK](https://fonts.google.com/noto)（开发机）。

---

## 前言

> **合作声明**
>
> | 成员 | 分工 |
> |---|---|
> | **Skywindfox** | 硬件验证与系统环境（开发板、Debian 系统）、CMake 工程与中控软件、文档 |
> | **FBI-0537** | 交叉编译环境（`armhf-toolchain/`）与构建脚本、LVGL 界面重构 |
>
> **约定**：改动对方负责的文件时**不要删他的注释与说明**；
> 不要动别人负责的区域，如需改动先在 issue 里说一声。
>
> 注：交叉编译环境**已统一到 `armhf-toolchain/`（容器）**，两人共用同一份 `Dockerfile`，
> 不再各维护一套。

- 本项目为省级大创项目（两人合作），**本仓库仅作个人存储，不建议对外使用**
- 目标：在 STM32MP135 + 正点原子开发板上，用自编译的 Debian Linux 实现**智能楼宇消防系统**
- 主要功能模块：（暂未填写）

**当前完成度**

| 描述 | 进度 |
|---|---|
| 板子上硬件基础测试 | 100% |
| 系统替换 | 100% |
| 系统环境 | 100% |
| 传感器 | 0% |
| PCB | 0% |
| 触发器 | 0% |
| 中控软件 | 5% |
| GUI 美化 | 0% |

---

## 目录

- [1. 项目构成](#1-项目构成)
- [2. 开发环境](#2-开发环境)
- [3. 交叉编译环境](#3-交叉编译环境)
- [4. 目标板环境](#4-目标板环境)
- [5. 构建与运行](#5-构建与运行)
- [6. VS Code 配置](#6-vs-code-配置)
- [7. 目录结构](#7-目录结构)
- [8. 设计要点](#8-设计要点)
- [9. 实测结果](#9-实测结果)
- [10. 待办](#10-待办)

> **动手改代码前先看 [WARNING.md](WARNING.md)** —— 踩坑记录、已知问题、未验证项都在那里。

---

## 1. 项目构成

| 目标 | 源文件 | 说明 | 能否交叉编译 |
|---|---|---|---|
| `halloworld-gui` | `src/gui.cpp` | **LVGL 图形界面**（X11 后端） | ✅ |
| `halloworld` | `src/main.cpp` | 控制台程序，打印 `hallo world` | ✅ |
| `test_greeting` | `tests/test_greeting.cpp` | 单元测试，`ctest` 调用 | ❌ 交叉产物跑不了 |
| `libfirecontrol.a` | `src/greeting.cpp` | 纯逻辑层静态库，上面几个都链接它 | ✅ |

---

## 2. 开发环境

| 项目 | 值 |
|---|---|
| 开发机 | Fedora 44 (Workstation) / Windows + Docker Desktop |
| 编译工具 | gcc 16.2.1、cmake 4.3.0、ninja 1.13.2、gdb 17.2 |
| GUI 依赖 | X11 + FreeType（容器内为 armhf 版） |
| 容器运行时 | `podman`（Fedora）或 `docker`（Windows），构建脚本自动选 |

原生构建需要：

```bash
# Fedora
sudo dnf install gcc gcc-c++ glibc-devel cmake ninja-build make gdb \
                 libX11-devel freetype-devel pkgconf-pkg-config
```

---

## 3. 交叉编译环境

**统一入口：`armhf-toolchain/`**，环境由仓库里的 `Dockerfile` 定义，随代码走。

```bash
cd FireControlApp
./armhf-toolchain/build-armhf.sh          # Linux / macOS（docker 或 podman 自动选）
.\armhf-toolchain\build-armhf.ps1         # Windows (Docker Desktop)
```

镜像站（`docker.io` 不可达时）：

```bash
BASE_IMAGE=docker.m.daocloud.io/library/debian:bookworm-slim \
    ./armhf-toolchain/build-armhf.sh
```

| 文件 | 作用 |
|---|---|
| `Dockerfile` | 环境定义：`debian:bookworm-slim`（与板子同版本）+ Debian 官方交叉编译器 + armhf 版 X11/freetype |
| `Dockerfile.build-armhf` | 构建期用：源码 COPY 进镜像层编译 |
| `toolchain-docker-armhf.cmake` | 容器内用的 CMake 工具链文件 |
| `build-armhf.sh` / `.ps1` | 两个平台的构建入口 |
| `verify-on-board.sh` | 上板前校验产物（架构 / ABI / 运行时依赖 / 字体 / X） |
| `README.md` | 工具链详细说明 |

**产物隔离**：每次构建写入 `build-armhf/TOOLCHAIN.txt`（工具链指纹），随产物拷到板上，
出问题时先看它确认「这个二进制是谁编的」。

### 另一条路径：Zig（只编纯逻辑层）

`cmake/toolchain-armhf.cmake` + `~/.local/bin/arm-linux-*`（Zig 0.16 包装脚本）
可以交叉编译**纯逻辑层与控制台程序**，速度快，但**没有 X11/freetype，编不了 GUI**。

```bash
./build.sh armhf        # 产出 build-armhf/ 里的逻辑层与 halloworld
armhf-env               # 检查这套环境是否就绪
```

---

## 4. 目标板环境

| 项目 | 值 |
|---|---|
| 硬件 | 正点原子 ATK-DLMP135（STM32MP135） |
| CPU | Cortex-A7 单核 1 GHz（ARMv7-A，VFPv3 + NEON） |
| 系统 | Debian 12 bookworm armv7l，内核 5.15.24 |
| 内存 | **437 MiB**（这是选型硬约束） |
| 屏幕 | 1024×600（正点原子 7 寸 RGB LCD，型号 7016） |
| 触摸 | Goodix 电容触摸屏，I2C-1 |
| 桌面 | Xorg `:0`，**无窗口管理器** |

**板上需要装的**：

```bash
sudo apt install g++ libx11-dev libfreetype-dev cmake     # 若要在板上原生编译
sudo apt install xinput x11-utils x11-apps                # 排查触摸/显示时才需要
```

**运行时依赖**（板上通常已具备，X 桌面自带）：
`libX11.so.6` / `libfreetype.so.6`，以及中文字体 `fonts-wqy-zenhei`。

**内存约束**是选型依据：裸 Xlib 约 2–4 MiB，GTK4 空程序要 40–80 MiB。
当前用的 LVGL + FreeType 实测常驻约 **136 MB / 437 MB**。

---

## 5. 构建与运行

### 5.1 `build.sh`（推荐）

```bash
./build.sh native      # 原生 x86-64：配置 + 构建 + ctest
./build.sh run         # 原生构建后运行【控制台程序】（不开窗口）
./build.sh gui         # 原生构建后打开【GUI 窗口】（1024x600）
./build.sh gui-full    # 原生 GUI，全屏
./build.sh armhf       # 交叉编译纯逻辑层 + 控制台（Zig，无 GUI）
./build.sh gui-armhf   # 交叉编译 GUI（容器，需 docker/podman）
./build.sh all         # 原生 + armhf
./build.sh clean       # 删除构建目录
./build.sh verify      # 只做 armhf 产物结构验证
```

**最常见的困惑**：`./build.sh run` 跑的是**控制台程序，不开窗口**。看窗口用 `./build.sh gui`。

### 5.2 直接用 CMake

```bash
# 原生
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/halloworld-gui --windowed      # 窗口模式（1024x600）
./build/halloworld-gui                 # 全屏

# armhf GUI：不要手工拼 cmake 命令，用工具链入口（见第 3 节）
./armhf-toolchain/build-armhf.sh
```

### 5.3 部署到板子

```bash
./armhf-toolchain/verify-on-board.sh root@<板子IP>      # 上板前先校验

scp build-armhf/halloworld-gui build-armhf/TOOLCHAIN.txt root@<板子IP>:~/
ssh root@<板子IP> 'DISPLAY=:0 ./halloworld-gui'
```

### 5.4 GUI 交互

```
┌──────────────────────────────────────────────┬────────┐
│                                              │ 【退出】│  ← 右上角
│                                              └────────┘
│                    halloworld                          │  ← 居中标题
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐          │
│  │ 功能 1 │ │ 功能 2 │ │ 功能 3 │ │ 功能 4 │          │  ← 底部占位
│  └────────┘ └────────┘ └────────┘ └────────┘          │
└────────────────────────────────────────────────────────┘
```

| 操作 | 效果 |
|---|---|
| 点右上角【退出】 | 退出程序 |
| 点底部【功能 1~4】 | 目前只打印提示（**功能待定**） |
| 按 `q` / `Esc` | 退出程序 |

按钮尺寸按窗口比例计算，高度限制在 36–64 px（触摸操作需要）。
**不支持双击窗口退出** —— 原因见 [WARNING.md C-6](WARNING.md#c-6-窗口映射瞬间可能收到杂散-buttonpress)。

---

## 6. VS Code 配置

`.vscode/` 下三个文件：`tasks.json`（10 个任务）、`settings.json`、`c_cpp_properties.json`。

`Ctrl+Shift+B` = 原生全量构建。GUI 相关：

| 任务 | 说明 |
|---|---|
| `CMake: 构建 GUI 并运行 (原生预览)` | 编 x86-64 版并弹出窗口 |
| `CMake: 构建 GUI armhf (交叉编译)` | 调 `build.sh gui-armhf`（容器） |

IntelliSense 走 `compile_commands.json`（CMake 导出），所以补全/跳转/报错与真实编译一致。
**前提是先跑一次配置**（`Ctrl+Shift+B` 会做）。

---

## 7. 目录结构

```
FireControlApp/
├── README.md / WARNING.md          说明 / 踩坑记录
├── CMakeLists.txt                  顶层构建定义
├── lv_conf.h                       LVGL 配置（只写覆盖项，其余走默认）
├── build.sh                        便捷构建封装
├── .gitattributes / .dockerignore / .gitignore
├── cmake/
│   └── toolchain-armhf.cmake       Zig 工具链（纯逻辑层，无 X11）
├── armhf-toolchain/                ★ 交叉编译 GUI 的唯一入口
├── src/
│   ├── greeting.h / greeting.cpp   纯逻辑层 → libfirecontrol.a
│   ├── main.cpp                    控制台程序入口
│   └── gui.cpp                     LVGL 图形界面 → halloworld-gui
├── third_party/lvgl/               LVGL v9.2.3 源码（已裁掉 docs/demos/tests/examples）
├── tests/test_greeting.cpp
└── .vscode/
```

构建产物（不纳入版本控制）：

```
build/               原生 x86-64
build-armhf/         armhf 产物（Zig 或容器），含 TOOLCHAIN.txt 指纹
build-debug/         Debug 构建
```

**LVGL 源码直接放进仓库**（不是 submodule、也不在 configure 时下载），所以断网也能编。
为控制仓库体积裁掉了 `docs/` `demos/` `examples/` `tests/` `scripts/` `.github/`
`.devcontainer/` 和 `env_support/cmsis-pack/`，裁完约 16 MB / 827 文件。
**升级 LVGL 时要保持同样的裁剪**，替换后重新跑一次构建确认。

---

## 8. 设计要点

- **逻辑层独立成静态库**：`greeting.cpp` → `libfirecontrol.a`，主程序与单元测试都链接它，
  这样逻辑代码能被测试覆盖、也能交叉编译（不依赖平台特有库）。
- **GUI 用 LVGL + X11 后端**：而不是 framebuffer —— 板上 `/dev/fb0` 归 Xorg 管，直写会抢屏。
  将来若去掉 Xorg，把显示初始化换成 `lv_linux_fbdev` + `lv_evdev` 即可，界面代码不用动。
- **中文字体走 FreeType 运行时加载**：改文案不用重新生成字库。字体按**文件路径**查找而非
  字体名，避免 fontconfig 静默替换成不含汉字的字体。
- **默认全屏**：板上没有窗口管理器，窗口位置尺寸得自己定。

各项的设计取舍与踩过的坑在 [WARNING.md](WARNING.md)。

---

## 9. 实测结果

| 项目 | 结果 |
|---|---|
| 原生构建 | 0 warning，`ctest` 1/1 |
| 原生 GUI | `halloworld-gui` 约 900 KB，1024×600 窗口正常 |
| 容器镜像构建 | ✅ 通过（`bookworm-slim` + gcc-arm 12 + cmake 3.25.1） |
| 镜像自检 | ✅ 能交叉编译 X11+FreeType 的 armhf 程序；pkg-config 指向 armhf |
| 容器内交叉编译 | ✅ LVGL 全量 + `halloworld-gui` 链接成功 |
| **armhf 产物** | **399,624 字节**，`ELF32 / ARM / hard-float`，依赖 `libX11` / `libfreetype` / `libgcc_s` / `libc` |
| 上板实测（LVGL 版） | ❌ **还没做** —— 见 [WARNING.md A-1](WARNING.md#a-当前遗留问题) |
| 上板实测（老 Xlib+Xft 版） | ✅ 曾通过（窗口正常、中文正常），但该版本已被 LVGL 版取代 |

---

## 10. 待办

- [ ] **在板子上实测 LVGL 版**：`verify-on-board.sh` 校验后运行，确认窗口、中文、触摸
- [ ] 板上实测触摸：点【退出】与占位按钮的命中是否准确、坐标是否偏移
- [ ] 确认全屏下字号观感（`kBigFontPx` / `kSmallFontPx`）
- [ ] 给字体候选表补上 Fedora 的路径（方便开发机预览中文，见 WARNING.md A-2）
- [ ] 决定是否需要极简窗口管理器（`matchbox` / `openbox`），否则所有程序只能全屏
- [ ] 填充 4 个占位按钮的实际功能
- [ ] 传感器模块（当前 0%）
