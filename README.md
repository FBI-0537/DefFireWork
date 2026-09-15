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
>
> **要在 Windows 上构建、或者要上板实测？看 [VERIFY-RUNBOOK.md](VERIFY-RUNBOOK.md)** ——
> 构建 / 传板 / 逐项验收的判据和证据模板；现状与待办见下面第 9、10 节。

---

## 1. 项目构成

| 目标 | 源文件 | 说明 | 能否交叉编译 |
|---|---|---|---|
| `deffire-gui-dev` | `src/gui.cpp` | **LVGL 图形界面**（X11 后端） | ✅ |
| `deffire-dev` | `src/main.cpp` | 控制台程序，打印 `hallo world` | ✅ |
| `test_greeting` | `tests/test_greeting.cpp` | 单元测试，`ctest` 调用 | ❌ 交叉产物跑不了 |
| `libfirecontrol.a` | `src/greeting.cpp` `src/usetools.cpp` `src/start.cpp` | 纯逻辑 / 硬件层静态库（`usetools` = 文件读写 + GPIO 读取，`start` = 板载设备控制），上面几个都链接它 | ✅ |

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

```powershell
.\armhf-toolchain\build-armhf.ps1 -BaseImage docker.m.daocloud.io/library/debian:bookworm-slim
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

### 旧路径：Zig（已废弃）

`cmake/toolchain-armhf.cmake` + `~/.local/bin/arm-linux-*`（Zig 0.16 包装脚本）曾用于
交叉编译**纯逻辑层与控制台程序**（没有 X11/freetype，编不了 GUI）。

统一到 `armhf-toolchain/` 的 Docker 环境后，`build.sh` 已删除对应的
`armhf` / `verify` 模式，这条路径不再有入口。工具链文件本身还留在仓库里，
但已经没有脚本引用它。

踩过的坑记录在 [WARNING.md](WARNING.md) 的 B 节 —— 那不是废纸，是换来的经验。

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
| 桌面 | Xorg `:0` + LXDE / **Openbox**（有窗口管理器，见下） |

> 桌面环境由板上 `neofetch` 实测确认：`DE: LXDE` / `WM: Openbox` / `Resolution: 1024x600`。

**有窗口管理器的后果**：`lv_x11_window_create()` 建的窗口会被 Openbox 接管 ——
开发机上（同样是 WM 环境）实测窗口被加上装饰，报出来的尺寸是 1074x687 而不是请求的
1024x600。要做真正的全屏，需要在建窗口后发 `_NET_WM_STATE_FULLSCREEN`（走 EWMH）。
详见 WARNING.md C-7。

**板上需要装的**：

```bash
sudo apt install g++ libx11-dev libfreetype-dev cmake     # 若要在板上原生编译
sudo apt install libgpiod-dev                             # 同上, 且要编 GPIO 读取时
sudo apt install xinput x11-utils x11-apps                # 排查触摸/显示时才需要
sudo apt install gpiod                                    # 排查 GPIO 时才需要 (gpiodetect / gpioinfo)
```

**运行时依赖**：
`libX11.so.6` / `libfreetype.so.6`（X 桌面自带），中文字体 `fonts-wqy-zenhei`，
以及 **`libgpiod2`**（`sudo apt install libgpiod2`）。

> ⚠ `libgpiod2` 是**硬依赖**：交叉编译出来的 `deffire-gui-dev` 链着
> `libgpiod.so.2`（`gpio_read_value()` 用它）。板上没有这个库时**整个界面起不来**
> ——动态链接器在 `main()` 之前就会报 `error while loading shared libraries`，
> 不是"传感器功能不可用"那么局部。`verify-on-board.sh` 会把缺失的库逐个列出来。
> 不想要这个依赖就 `WITH_GPIOD=OFF` 重新构建（见 §5）。

**内存约束**是选型依据：裸 Xlib 约 2–4 MiB，GTK4 空程序要 40–80 MiB。
当前用的 LVGL + FreeType 实测常驻约 **136 MB / 437 MB**。

---

## 5. 构建与运行

### 5.1 `build.sh`（推荐）

```bash
./build.sh native      # 原生 x86-64：配置 + 构建 + ctest
./build.sh gui-full    # 原生 GUI，全屏
./build.sh gui-armhf   # 交叉编译 GUI（容器，需 docker/podman）
./build.sh all         # 原生 + gui-armhf
./build.sh clean       # 删除构建目录
```

`gui-armhf` 后面的额外参数会转给 `armhf-toolchain/build-armhf.sh`：

```bash
./build.sh gui-armhf --rebuild      # 先重建环境镜像再编译
./build.sh gui-armhf --shell        # 进容器交互
```

> Zig 旧路径（`run` / `gui` / `armhf` / `verify` 四个模式）**已删除**。
> 想看开发机的窗口模式，直接跑二进制：`./build/deffire-gui-dev --windowed`。

### 5.2 直接用 CMake

```bash
# 原生
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/deffire-gui-dev --windowed      # 窗口模式（1024x600）
./build/deffire-gui-dev                 # 全屏

# armhf GUI：不要手工拼 cmake 命令，用工具链入口（见第 3 节）
./armhf-toolchain/build-armhf.sh
```

### 5.3 部署到板子

项目是**开发机交叉编译、产物拷到板上跑**，板上不编译（性能不够）。所以没有
"板上 `git pull`"这条路，一切都从开发机推。

**板上统一用 `~/Desktop`**：所有产物和脚本都放那里。

```bash
./armhf-toolchain/verify-on-board.sh fbi@<板子IP>        # 上板前先校验产物与本板匹配
./armhf-toolchain/deploy-to-board.sh fbi@<板子IP>        # 传输产物 + 权限脚本到 ~/Desktop
./armhf-toolchain/deploy-to-board.sh --run fbi@<板子IP>  # 传完直接启动
```

必须显式给 `用户@IP`：开发机上通常解析不了板子的短主机名。板子的普通用户是 `fbi`。

**首次部署还要在板上做一次权限配置**（否则界面里点 LED / 蜂鸣器没反应）：

```bash
ssh fbi@<板子IP>
sudo bash ~/Desktop/setup-board-permissions.sh fbi
sudo reboot
```

原因见 [WARNING.md](WARNING.md) 的「sysfs 权限」一节 —— `/sys/class/leds/*/brightness`
默认是 root 只写，普通用户写的失败被静默吞掉了。

### 5.4 GUI 交互

界面是**深色工业简约风**的三段式面板：顶部标题栏 / 中间内容区 / 底部操作栏。

```
首页                                             LED / 蜂鸣器调试页
┌──────────────────────────────────────────┐   ┌──────────────────────────────────────────┐
│ ▍FireControlApp        ● 运行中  [ 退 出 ]│   │ ▍FireControlApp        ● 运行中  [ 退 出 ]│ ← 顶栏 (屏高 11%)
├──────────────────────────────────────────┤   ├──────────────────────────────────────────┤
│                                          │   │                                          │
│                halloworld                │   │                halloworld                │ ← 内容区
│                   ─────                  │   │                   ─────                  │
│                   首页                    │   │                LED 调试页                 │ ← 页名 · 上一步结果
│                                          │   │                                          │
├──────────────────────────────────────────┤   ├──────────────────────────────────────────┤
│ ┌─────────┐┌─────────┐┌─────────┐        │   │┌───────┐┌───────┐┌───────┐┌───────┐       │ ← 底栏
│ │ LED 调试 ││蜂鸣器 调试││  功能 8  │        │   ││LED 开 ││LED 关 ││LED 心跳││  返回 │       │   (按钮高 + 2×边距)
│ └─────────┘└─────────┘└─────────┘        │   │└───────┘└───────┘└───────┘└───────┘       │
└──────────────────────────────────────────┘   └──────────────────────────────────────────┘
```

界面是**分页**的：顶栏（品牌 / 状态灯 / 退出）三页完全一致，切页只重建底栏按钮；
每页最多 4 个按钮，排一行。按钮表在 `gui.cpp` 的 `kHomeButtons` / `kLedButtons` /
`kBuzzerButtons`（页表是 `kPages`），布局按数量自动排（`kPerRow` 控制每行几个，
数组长度用 `countOf()` 取，不要手写数字）。

| 操作 | 效果 |
|---|---|
| 点顶栏右侧 **【退出】** | 退出程序（三页都一样） |
| 首页点 **【LED 调试】/【蜂鸣器 调试】** | 进对应调试页 |
| 调试页里点 **开 / 关 / 心跳** | 控制板载 `sys-led` / `beep`。板上需要先配权限（见 5.3） |
| 调试页末位 **【返回】** | 回首页 |
| 点【功能 8】 | 占位，打印提示 |
| 按 `q` / `Esc` | 退出程序（三页都一样） |

改界面前先读 [workflow.md](workflow.md) §3 的约定（`Action::Nothing` 必须留在枚举
第一位、`switch` 不写 `default`、切页只置标志、别改控制台那行的格式）。

打印坐标是故意留的：板上还没验证过触摸坐标是否偏移（见第 10 节待办），
点一下就能看出来。

**配色**（全部集中在 `gui.cpp` 顶部一段常量里，换风格只改那一段）：

| 用途 | 色值 | 说明 |
|---|---|---|
| 屏幕底 | `#121417` | 近黑，强光下不反光、夜间不刺眼 |
| 顶栏 / 底栏 | `#1A1D21` | 比底色略亮，把操作区分出来 |
| 按钮表面 | `#23272E` | 已实现的按钮 |
| 描边 / 分隔线 | `#2E343B` | 1px 走线 |
| 主文字 | `#E6E8EA` | 近白 |
| 次要文字 | `#8A9199` | 状态说明、未实现按钮 |
| 强调色（安全琥珀） | `#FFB020` | 品牌竖条、标题短线、**按下态描边** |
| 状态灯 | `#35C46A` | 运行中 |

**实现上的几个点**：

- **三段式**由 `buildHeader` / `buildContent` / `buildFooter` 三个函数搭出来，
  容器一律透明无描边（`createPane`），顶栏底栏的 1px 分隔线用 `border_side`
  只画一条边（`createBar`）。
- **布局全用 flex**：顶栏是 `SPACE_BETWEEN`（品牌 / 状态灯 / 退出），底栏每个按钮
  `flex_grow=1` 自动等宽，间距 = 屏宽的 1%。换屏幕尺寸不用改坐标。
- **未实现的按钮用"压暗"表达**：底色 = 屏幕底、描边和文字都取次要色，一眼能看出
  还没做（早期版本用双层边框，已改掉）。
- **按下反馈是即时的**：底色抬一档 + 描边点亮成强调色，同时 `lv_conf.h` 里把
  样式过渡动画关掉了（默认 80 ms），触摸要的就是"按下去立刻有回应"。
- **状态灯现在只表示"程序在跑"**（固定的绿灯 + "运行中"）。以后接上传感器/联动
  状态时，改 `buildHeader` 里的文字和灯色即可。
- **窗口尺寸**只在启动时算一次（`--windowed` 只有开发机预览时才用）。
  板上有 Openbox（见第 4 节），窗口会被 WM 接管、加装饰 —— 按屏幕尺寸建窗口**不等于**
  真全屏，要真铺满得发 `_NET_WM_STATE_FULLSCREEN`（见 WARNING.md C-7 与第 10 节待办）。
- **字体**：主标题 40 px、标题栏与按钮 18 px、状态文字 16 px；中文字体在启动时
  按候选表找一个字体**文件**（而不是字体名，避免 fontconfig 静默替换成不含汉字的字体）。
- **`q` / `Esc` 为什么能全局生效**：LVGL 的键盘输入只发给"输入组里当前被聚焦的
  对象"，组里没有聚焦对象时按键会被直接丢掉。所以启动时把**屏幕本身**加进默认
  组并聚焦它（按钮不进组，点按钮不会把焦点抢走）。
- **启动后 400 ms 内的点击会被忽略**：窗口刚映射时可能收到遗留的杂散点击，
  见 [WARNING.md C-6](WARNING.md#c-6-窗口映射瞬间可能收到杂散-buttonpress)。
- **窗口模式下会画一个跟随鼠标的小指针**（强调色）：LVGL 的 X11 后端会把 X 光标
  隐藏掉，板上是触摸屏不需要，但开发机预览时不画个指针就不知道鼠标在哪。

**注意**：**不支持"双击窗口退出"** —— 那是早期行为，已移除。原因见
[WARNING.md C-6](WARNING.md#c-6-窗口映射瞬间可能收到杂散-buttonpress)：

按钮在 1024×600 上的实际尺寸（按窗口比例算，换屏幕不用改代码）：

| 页面 | 按钮 | 位置 (x, y) | 尺寸 |
|---|---|---|---|
| 任意页 | 退出（顶栏右侧） | (922, 13) | 92×40 |
| 首页 | LED 调试 / 蜂鸣器 调试 / 功能 8 | (10, 536) / (348, 536) / (686, 536) | 328×54 |
| 调试页 | 开 / 关 / 心跳 / 返回 | (10, 536) / (263, 536) / (516, 536) / (769, 536) | 243×54 |

- 底栏按钮高 54 px 是按触摸操作设计的（手指触点约 40–50 px），代码里限制在
  36–64 px；顶栏那个退出键矮一些（40 px），因为它不是高频操作。
- 宽度由 flex 均分得出：首页 3 个 → (1024−20−2×10)/3 = 328；调试页 4 个 →
  (1024−20−3×10)/4 = 243.5。整数取整后可能有 ±1 像素的差异（表里的 x 按 328 / 243 算）。
- 顶栏高 66 px（屏高 11%），底栏高 74 px（按钮 54 + 上下边距 10×2），
  中间内容区 460 px。底栏高度按**所有页里最多的行数**算（`maxRows()`），
  所以切页时底栏高度不变、内容区不会上下跳。

### 5.5 可选依赖（CMake 选项）

| 选项 | 默认 | 作用 | 不满足时 |
|---|---|---|---|
| `WITH_GUI` | `AUTO` | 编 LVGL 界面（`deffire-gui-dev`） | 原生：警告并跳过；交叉：`ON` 时直接报错 |
| `WITH_GPIOD` | `AUTO` | 编 `gpio_read_value()`（GPIO 开关量输入，传感器用） | 明确打印一句"不编入产物"并继续；`ON` 时直接报错 |

```bash
cmake -B build -S . -DWITH_GPIOD=OFF     # 完全不依赖 libgpiod
cmake -B build -S . -DWITH_GPIOD=ON      # 必须要有可用的 libgpiod, 否则报错
```

`AUTO` 的意思是"有**可用版本**就编、否则跳过，**并且说一声**"——跳过时
`gpio_read_value()` 仍然存在，但会打印一行提示并返回 `-1`（不静默返回"低电平"
这种假数据）。

> ⚠ **只支持 libgpiod v1**：代码里用的是 `gpiod_chip_open_by_label` /
> `gpiod_line_request_input` 这套 v1 API，**v2 把这些符号全删了**。
>
> | 环境 | libgpiod | 结果 |
> |---|---|---|
> | armhf 容器 / 板子（Debian 12 bookworm） | 1.6.3 | ✅ 编入 |
> | Fedora 44（原生开发机） | 2.2.5 | ❌ 找到但不编入（`ON` 时报错） |
>
> 所以**原生 Fedora 构建拿不到这个功能**，装了 `libgpiod-devel` 也一样；要用就得先把
> `gpio_read_value()` 移植到 v2 API。交叉编译/板上不受影响。

板载 LED / 蜂鸣器**不需要** libgpiod（走 `/sys/class/leds/*`）；要它的是接在
GPIO 上的火焰/人体/光电那类开关量传感器（见第 10 节待办）。

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
│   └── toolchain-armhf.cmake       Zig 工具链（**已废弃**，无脚本引用）
├── armhf-toolchain/                ★ 交叉编译 GUI 的唯一入口
├── src/
│   ├── greeting.h / greeting.cpp   纯逻辑层 → libfirecontrol.a
│   ├── main.cpp                    控制台程序入口
│   └── gui.cpp                     LVGL 图形界面 → deffire-gui-dev
├── third_party/lvgl/               LVGL v9.2.3 源码（已裁掉 docs/demos/tests/examples）
├── tests/test_greeting.cpp
└── .vscode/
```

构建产物（不纳入版本控制）：

```
build/               原生 x86-64
build-armhf/         armhf 产物（容器编译），含 TOOLCHAIN.txt 指纹
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
- **默认全屏**：窗口按屏幕实际尺寸铺满（板上跑着 Openbox，见「板上运行环境」）。

各项的设计取舍与踩过的坑在 [WARNING.md](WARNING.md)。

---

## 9. 实测结果

| 项目 | 结果 |
|---|---|
| 原生构建 | 0 warning，`ctest` 1/1 |
| 原生 GUI | `deffire-gui-dev` 约 900 KB，1024×600 窗口正常；首页 / LED 调试页 / 蜂鸣器调试页三页切页与按钮派发已实测 |
| 容器镜像构建 | ✅ 通过（`bookworm-slim` + gcc-arm 12 + cmake 3.25.1） |
| 镜像自检 | ✅ 能交叉编译 X11+FreeType 的 armhf 程序；pkg-config 指向 armhf |
| 容器内交叉编译 | ✅ LVGL 全量 + `deffire-gui-dev` 链接成功 |
| **armhf 产物** | **407,936 字节**，`ELF32 / ARM / hard-float`，依赖 `libX11` / `libfreetype` / **`libgpiod`** / `libstdc++` / `libgcc_s` / `libc`；同源同镜像重建 sha256 逐字节一致（可复现） |
| 上板实测（LVGL 版） | ❌ **还没做** —— 见 [WARNING.md A-1](WARNING.md#a-当前遗留问题) |
| 上板实测（老 Xlib+Xft 版） | ✅ 曾通过（窗口正常、中文正常），但该版本已被 LVGL 版取代 |

---

## 10. 待办

- [ ] **在板子上实测 LVGL 版**：`verify-on-board.sh` 校验后运行，确认窗口、中文、触摸
- [ ] 板上实测触摸：点【退出】与占位按钮的命中是否准确、坐标是否偏移
- [ ] 确认全屏下字号观感（`kBigFontPx` / `kSmallFontPx`）
- [ ] 给字体候选表补上 Fedora 的路径（方便开发机预览中文，见 WARNING.md A-2）
- [ ] **实测 Openbox 下是否真全屏**：板上跑一次无参 `./deffire-gui-dev`，看窗口有没有被
      加标题栏 / 留边距。没铺满就要发 `_NET_WM_STATE_FULLSCREEN`（见 WARNING.md C-7）
- [ ] 填充占位按钮【功能 8】的实际功能（其余按钮都已接上板载设备）
- [ ] 传感器模块（当前 0%）：骨架已有（`gpio_read_value()` + `WITH_GPIOD`），但**要先上板
      确认传感器接在哪个 gpiochip / 哪条线**、那些线是否已被内核占用 —— 见
      [workflow.md](workflow.md) §2.3
