# armhf-toolchain — 统一的 armhf 交叉编译环境（Docker）

**这是本项目**唯一**的 armhf 交叉编译环境。** 目的是消除"每人一套工具链、
产出同名但来源不明的二进制"这个问题。

---

## 为什么要有这个目录

在这之前，armhf 二进制可能来自三套互不相同的环境：

| 来源 | 编译器 | glibc | GUI 依赖 | sysroot 在哪 |
|---|---|---|---|---|
| 旧: Fedora + Zig | Zig 0.16 (clang 21) | Zig 内建 | X11 + **Xft** + fontconfig | `~/Code/armhf-sysroot`（仓库外） |
| 旧: Windows + Docker | Debian gcc | 镜像内 | X11 + freetype | Docker 镜像（仓库外） |
| 板上原生编译 | 板上 g++ | 板载 | 板载 | 不需要 |

三者编译器和 libc 都不同，产出的二进制**同名**（`halloworld-gui`），
出问题时第一步就得花时间确认"这是谁编的、它要的库本板有没有"。

**现在统一到这里**：环境由本目录的 `Dockerfile` 定义，随仓库走，
任何人构建出来的工具链完全一致。

---

## 快速开始

```bash
# 一次性：构建镜像（首次约几分钟）
docker build -t firecontrol-armhf:bookworm armhf-toolchain/

# 构建项目
./armhf-toolchain/build-armhf.sh              # Linux / macOS
.\armhf-toolchain\build-armhf.ps1             # Windows (Docker Desktop)

# 产物在 build-armhf/，其中 TOOLCHAIN.txt 记录工具链指纹
```

本机有 podman 也能用 —— 脚本会自动在 `docker` / `podman` 之间选择：

```bash
podman build -t firecontrol-armhf:bookworm armhf-toolchain/
./armhf-toolchain/build-armhf.sh
```

---

## 文件说明

| 文件 | 作用 |
|---|---|
| `Dockerfile` | 交叉编译环境定义。基于 `debian:bookworm-slim`（与目标板同版本），装 Debian 官方交叉编译器 + armhf 版 X11/freetype |
| `toolchain-docker-armhf.cmake` | CMake 工具链文件（容器内使用）。与旧的 `cmake/toolchain-armhf.cmake` 区分开 |
| `build-armhf.sh` | Linux/macOS 构建脚本（docker 或 podman） |
| `build-armhf.ps1` | Windows 构建脚本（Docker Desktop） |
| `verify-on-board.sh` | **在目标板上**校验产物：架构、ABI、运行时依赖、中文字体、X server |

---

## 隔离设计（这是本目录的重点）

"配好环境"只是第一步，**防止三种产物混淆**才是关键。做法有三层：

### 1. 产物目录带工具链指纹

每次构建都会在 `build-armhf/TOOLCHAIN.txt` 写入：

```
image      = firecontrol-armhf:bookworm
base       = debian:bookworm-slim
compiler   = 12.2.0
triplet    = arm-linux-gnueabihf
cmake      = 3.25.1
built_at   = 2026-09-12T14:22:31Z
host_runtime = docker
host_arch    = x86_64
```

这个文件**随产物一起拷到板上**，所以板上也能查"这个二进制是谁编的"。

### 2. CMake 配置阶段校验并写入指纹

`CMakeLists.txt` 在 configure 时把工具链信息写入 `TOOLCHAIN.txt`；
如果目标目录里已有**不同**工具的指纹，会给出警告而不是静默混用。

### 3. 上板前用 `verify-on-board.sh` 校验

```bash
./armhf-toolchain/verify-on-board.sh root@<板子IP>
```

它会检查：

- 32 位 ELF / ARM 架构 / **硬浮点 ABI**（板子是 `arm-linux-gnueabihf`）
- **每个运行时依赖在板上都存在**（`readelf -d` 的 NEEDED 逐个查）—— 缺一个就报错
- 中文字体（文泉驿正黑）在不在 —— 不在则界面汉字会变方框
- X server 在跑（`/tmp/.X11-unix/X0`）
- 打印该产物的工具链指纹

**产物与本板匹配才建议运行**，否则先换工具链重编。

---

## 环境内容（Dockerfile 做了什么）

```
基础      debian:bookworm-slim          ← 与目标板 Debian 12 同版本, glibc 不错配
编译器    gcc-arm-linux-gnueabihf       ← Debian 官方源, 版本随仓库
          g++-arm-linux-gnueabihf
          binutils-arm-linux-gnueabihf
构建工具  cmake / make / ninja-build / pkg-config
armhf 库  libx11-dev:armhf              ← LVGL 的 X11 显示/输入后端
          libfreetype-dev:armhf         ← 中文字形
```

**依赖发现机制**：Debian multiarch 下，armhf 库装在
`/usr/lib/arm-linux-gnueabihf/`，与宿主的 `x86_64-linux-gnu` 天然隔离；
交叉编译器默认就搜多架构目录。所以不需要像手工 sysroot 那样维护
include/lib 路径列表 —— 版本永远和 Debian 仓库一致，不会漂移。

**为什么是 amd64 镜像**：这是**交叉编译**环境，跑在开发机上，产出 armhf 二进制。
不需要 binfmt/qemu 模拟，构建也快。

---

## 镜像内自检

`Dockerfile` 在构建阶段就编译一个含 X11 + FreeType 的探针程序，
并检查产物是 ARM 且带硬浮点 ABI。**自检失败会让 `docker build` 直接失败**，
不会留下一个"看起来能用实则残缺"的镜像。

所以：**镜像构建成功 = 工具链可用**。

---

## ⚠️ 已知限制

- **Windows 上需要 Docker Desktop 处于运行状态**，脚本会检测并给出提示。
- 本 Dockerfile 已在 **Windows + Docker Desktop** 上实测通过（2026-09-12）。
  首次实测修掉了三个会让构建**必然失败**的问题：自检缺 `-I/usr/include/freetype2`、
  slim 镜像里没有 `file`、`deb.debian.org` 间歇性断连。详见 `Dockerfile` 注释。
- 默认软件源已换成**清华 TUNA** —— `deb.debian.org` 在部分网络下会"前 10 MB 能下、
  后面直接连不上"，导致镜像构建随机失败。要用官方源：
  `docker build --build-arg APT_MIRROR=deb.debian.org -t firecontrol-armhf:bookworm armhf-toolchain/`
- **Windows 检出注意**：仓库根目录的 `.gitattributes` 已把 `*.sh` / `Dockerfile` 锁成 LF。
  Git for Windows 默认 `core.autocrlf=true`，会把它们 checkout 成 CRLF，那样的 `.sh`
  拿到板子或容器里执行会报 `bad interpreter: No such file or directory`。
  已经用 CRLF 检出的工作区需要重新检出才会变成 LF：`git rm --cached -r . && git reset --hard`。
- 旧的 `cmake/toolchain-armhf.cmake`（Zig 版，编译**纯逻辑层与控制台程序**）
  **仍然保留** —— 它不需要 X11，用 Zig 编很快，作为轻量路径继续可用。
  只有 **GUI** 必须走本目录的 Docker 环境。
