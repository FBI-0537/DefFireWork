# WARNING — 踩坑记录与已知问题

本项目在实现过程中踩过的坑、以及当前仍未解决的问题，集中记录在这里。
**动手改任何代码前建议先扫一遍**，尤其是交叉编译与字体相关的部分。

按来源分四类：

- [A. 当前遗留问题](#a-当前遗留问题)
- [B. 交叉编译与构建工具链](#b-交叉编译与构建工具链)
- [C. 界面与字体](#c-界面与字体)
- [D. 环境与工具](#d-环境与工具)
- [E. 未遇到但值得警惕的](#e-未遇到但值得警惕的)

---

## A. 当前遗留问题

| # | 问题 | 说明 |
|---|---|---|
| 1 | **LVGL 版的 armhf 产物还没在板上实测过** | 只做到交叉编译验证（构建 0 警告、产物为 armv7 hard-float、`NEEDED` 为 libX11/libfreetype）。**上板先点一遍【退出】和底部按钮**，确认触摸坐标、中文显示、字体路径都对 |
| 2 | **开发机上跑 GUI 中文会显示成方框** | 字体候选表里 6 条路径都是 Debian 布局，Fedora 上一条都不存在。临时绕开：`./build/halloworld-gui --font=/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc`。**板上不受影响**（`wqy-zenhei.ttc` 在候选表第一条） |
| 3 | **`halloworld` 拼写** | `hallow` 通常是 `hollow` 的笔误。沿用最初的文件名未改。改的话要同时动 `CMakeLists.txt` 的 `add_executable`、`tests/` 里的字符串、`build.sh` 的产物路径 |
| 4 | **CMake 不自动扫描源文件** | 新增 `.cpp` 必须手动加到 `CMakeLists.txt`（`add_library` / `add_executable`），否则不会被编译 |
| 5 | **`build.sh run` 不开窗口** | 它跑的是控制台程序 `halloworld`。要看窗口用 `build.sh gui` |
| 6 | **armhf 的 GUI 不能在开发机运行** | 架构不同 + 需要板子的 libX11。必须 scp 到板子 |
| 7 | **容器内首次全量编译较慢** | LVGL 有 311 个 `.c`。容器方案是 COPY 进镜像层，**没有增量编译**，每次全量 |
| 8 | **repo 目录的 SELinux 标签可能被改过** | 如果曾用 `:Z` 跑过容器，标签会变成 `container_file_t` 且 `restorecon` 会拒绝恢复。修法见 [D-3](#d-3-z-会永久改写宿主目录的-selinux-标签) |

---

## B. 交叉编译与构建工具链

### B-1. Zig 0.16 的 `-fsyntax-only` 是坏的

CMake 探测编译器时用它试编，Zig 下直接报 `FileNotFound`，configure 阶段就失败。
解法（每个 toolchain 文件里都有）：

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

静态库探测不需要链接，绕开该问题。**这是 CMake + Zig 能跑通的关键。**

### B-2. glibc 目标不支持静态链接

```
error: libc of the specified target requires dynamic linking
```

静态必须换 musl：`arm-linux-musleabihf-gcc -static`。

### B-3. 无法事后 strip ARM 文件

Fedora 的 binutils 只编了 x86 BFD 后端，不认 ARM；`zig objcopy` 报 `unimplemented`。
**瘦身只能靠链接期 `-s`**：

```cmake
if(IS_CROSS_BUILD)
    target_link_options(<target> PRIVATE -s)
endif()
```

不加会带 debug_info，体积差一个数量级。

### B-4. `zig ar` / `ranlib` 不接受 `-target`

包装脚本里已经去掉了。别自作主张加回去。

### B-5. Zig 0.16 没有 `readelf`/`nm`/`size`/`strings`/`ld`/`as` 子命令

ELF 分析改用主机 GNU binutils：`readelf` 读 ELF 头是架构无关的，能正常处理 ARM 文件。

### B-6. Zig 不带任何 X11 头文件

`linux/*.h`（内核 UAPI）Zig 自带 611 个，但 **`X11/*.h` 一个都没有** —— 它是外部库。
所以 GUI 交叉编译必须有 ARM sysroot，这也是 `armhf-toolchain/` 存在的理由。

### B-7. Dockerfile 的 RUN 里不能用 `$$` 逃逸

`$$(cmd)` 在 podman 下**没被正确转义**，实测变成了 PID 拼接（`ELF_H=1(...)`），导致自检误判失败。
Docker 与 podman 对 `$` 的替换语义不一致，所以自检脚本里**刻意不用任何 shell 变量**，全用管道。

### B-8. 构建期自检必须用 `readelf` 而不是 `file`

`debian:bookworm-slim` **没装 `file`**。用 `readelf`（binutils 自带）直接读 ELF 头，
一次就能确认 `Class=ELF32` / `Machine=ARM` / `hard-float ABI`，比 `file` 更准，也不必多装一个包。

### B-9. freetype 的头文件不在默认搜索路径

`ft2build.h` 在 `/usr/include/freetype2`。**不要手写 `-I`** —— 用 pkg-config：

```dockerfile
arm-linux-gnueabihf-gcc ... $(pkg-config --cflags --libs x11 freetype2)
```

并加一条断言，防止配错 `.pc` 时静默链到 x86-64：

```dockerfile
pkg-config --libs x11 freetype2 | grep -q 'arm-linux-gnueabihf'
```

### B-10. `xft.pc` 把 `x11` 放在 `Requires.private`

`pkg-config --libs xft` **默认不输出私有依赖**，只写 `xft` 会链接失败：

```
DSO missing from command line
```

显式写全：`pkg_check_modules(XFT QUIET xft x11)`。

### B-11. pkg-config 不会输出 `-I/usr/include`

它认为是默认路径。但**交叉编译时编译器不把 sysroot 的 include 当默认**，
所以还要显式加 `-isystem $SYSROOT/usr/include`。

### B-12. CMake 的 `FindX11` 不给传递依赖的头文件路径

`find_library` 只找 `/usr/lib`，而 ARM 库在 `/usr/lib/arm-linux-gnueabihf/`。
**只设 `CMAKE_FIND_ROOT_PATH` 不够** —— 它只给已知搜索路径加前缀，不会猜出 Debian 的三元组目录。
必须显式加 `CMAKE_LIBRARY_PATH` / `CMAKE_INCLUDE_PATH`，或者干脆改用 pkg-config。

### B-13. `deb.debian.org` 在部分网络下会"下到一半断"

表现为前 10 MB 正常、之后 `Unable to connect`，镜像构建随机失败。
镜像默认源已换成清华 TUNA，可用 build arg 覆盖：

```bash
docker build --build-arg APT_MIRROR=deb.debian.org -t firecontrol-armhf:bookworm armhf-toolchain/
```

### B-14. 容器 bind mount 在部分环境是只读的

`docker run -v <仓库>:/work` 后容器内写不了：

```
mkdir: cannot create directory '/work/build-armhf/CMakeFiles': Permission denied
```

**与 SELinux / 属主无关** —— 实测干净的 `user_home_t` 目录同样写不了。
所以本项目改用 `Dockerfile.build-armhf` + `COPY`：源码复制进镜像层编译，
产物用 `podman cp` 取回宿主，**全程不依赖挂载**。代价是没有增量编译。

### B-15. 合并冲突时容易漏掉 RUN 的续行反斜杠

在 Dockerfile 里改冲突块时若漏掉行尾 `\`，后面的命令会被当成新指令：

```
Error: Unknown instruction: "RM"
```

改完建议扫一眼每个 RUN 块的行尾。

---

## C. 界面与字体

### C-1. X11 核心位图字体画不出汉字

核心字体只有 **ISO-8859-1**（Latin-1）编码，没有汉字字形。
用 `XDrawString` 画中文，UTF-8 多字节会被当成多个 Latin-1 字符各画一个 —— 实测显示成 `蚂蚁` 之类。

解法：走 **Xft + FreeType + fontconfig**（老版），或 **LVGL + FreeType**（当前版）。

### C-2. `XftFontOpenName` 对不存在的字体名**不返回 NULL**

fontconfig 会静默做替换（substitution），返回一个替补字体。
如果替补结果不含汉字，中文就画成方框，而**程序完全不知道**自己拿到了错字体。

这就是"同代码同二进制，开发机正常、另一台机器变方框"的原因 —— 结果取决于各机器 fontconfig 的替换策略。

**当前版本改为按文件路径查找**（`kFontCandidates`），比按字体名可靠。
若以后改回按名查找，必须自己校验字形覆盖（`XftCharExists`）。

### C-3. 字体候选表是 Debian 布局

`kFontCandidates` 里的路径来自 Debian（`/usr/share/fonts/truetype/wqy/...`）。
Fedora 上这些路径**全部不存在**，所以开发机预览会显示方框（见 [A-2](#a-当前遗留问题)）。
要同时支持两个系统，需要把 Fedora 的路径也加进候选表。

### C-4. LVGL 集成时踩的三个坑

| 坑 | 说明 |
|---|---|
| `LV_CONF_PATH` 必须在 `add_subdirectory` **之前**设置 | LVGL 自己的 CMake 会把它变成编译宏 `-DLV_CONF_PATH=...`。找不到这个文件时 LVGL 会退回全默认配置（16 位色、没开 X11/FreeType），编出来的界面是另一回事 |
| LVGL 是第三方代码，别参与"零警告"要求 | 用 `target_compile_options(lvgl PRIVATE -w)`。只放过 `lvgl` 目标，`halloworld-gui` 仍保持零警告 |
| LVGL 的 FreeType 后端要 `ft2build.h` | 见 [B-9](#b-9-freetype-的头文件不在默认搜索路径)，include 路径要挂到 `lvgl` 目标上 |

### C-5. 触摸在 X11 里就是普通 Button 事件

```
Goodix 触摸面板 ──I2C──▶ 内核 goodix 驱动 ──▶ /dev/input/event0
                                                      │
                                          X server (libinput/evdev)
                                                      │
                            应用收到 ButtonPress / MotionNotify
```

**单点触摸就是普通的 `ButtonPress`，坐标在 `xbutton.x/y`** —— 不需要 XInput2
（XI2 的价值在多指手势和压力值）。排查触摸时：

```bash
dmesg | grep -iE 'goodix|gt91|touch'      # 内核认到没
xinput list                                # X server 认到没
xev                                        # 看原始事件
```

### C-6. 窗口映射瞬间可能收到杂散 `ButtonPress`

实测 3 次里约有 1 次，是竞态。若不处理，会"一启动就触发某个按钮"甚至直接退出。

**已缓解**：忽略映射后 400 ms 内的点击（用单调时钟，不用 X 事件时间戳 ——
`XExposeEvent` **没有 `time` 字段**，拿不到首帧时刻）。根因（X server/WM 行为）不在本项目控制内。

### C-7. 板上没有窗口管理器

所以默认全屏，自己用 `XMoveResizeWindow` 占满屏幕。窗口没有标题栏、边框、关闭按钮 ——
关闭只能靠界面里的【退出】按钮或 `q`/`Esc`。

---

## D. 环境与工具

### D-1. `readelf` 的字段名会被 locale 本地化

中文环境下 `Machine` 显示为「机器」，导致 `grep 'Machine:'` 之类的解析拿到空值。
**所有解析 ELF 输出的脚本都要强制 `LC_ALL=C`。**

### D-2. 验证脚本缺工具时会"静默通过"

`verify-on-board.sh` 早期版本用不带前缀的 `readelf`。最小化 Debian 上它未必装，
缺它时「运行时依赖是否齐全」**一个库都不查、输出为空**，却仍打印
`==== 结论: 产物与本板匹配, 可以运行 ====`。

**这是最危险的误报类型**（工具缺失 → 检查退化成空操作 → 给出肯定结论）。
现已修：开头对 `file`/`ldconfig`/`readelf` 做依赖工具自检，缺哪个显式报 ✗ 并置 `fail=1`；
`readelf` 找不到时退回 `arm-linux-gnueabihf-readelf`。
**写类似的检查脚本时注意这个模式。**

### D-3. `:Z` 会永久改写宿主目录的 SELinux 标签

Fedora 默认 SELinux Enforcing，容器默认读不到挂进来的宿主目录。两种解法：

| 做法 | 后果 |
|---|---|
| `:Z` | **永久**给宿主目录打 `container_file_t` 标签，而且 `restorecon` 会以 "customized by admin" 为由**拒绝恢复** |
| `--security-opt label=disable` | 只对本容器关闭 SELinux 隔离，**不动宿主标签**（本项目采用） |

如果目录已经被 `:Z` 污染过，恢复（需要 sudo）：

```bash
sudo semanage fcontext -D
sudo restorecon -R -v ~/FireControlApp
```

### D-4. Git for Windows 默认把 `.sh` 检出成 CRLF

`core.autocrlf=true` 是 Git for Windows 的默认值。CRLF 的 shell 脚本进容器或板子会报：

```
/usr/bin/env bash^M: bad interpreter: No such file or directory
```

仓库根的 `.gitattributes` 已把 `*.sh` / `Dockerfile` 锁成 `eol=lf`。
**已经用 CRLF 检出的工作区需要重新检出**才会变 LF：

```bash
git rm --cached -r . && git reset --hard     # 或直接重新 clone
```

### D-5. PowerShell 5.1 的三个坑

| 坑 | 现象 | 修法 |
|---|---|---|
| 脚本文件缺 UTF-8 BOM | PS 5.1 按 ANSI(gb2312) 读，中文被解错后**语法直接崩溃**（`Missing closing ')'`）| 文件保存为带 BOM 的 UTF-8 |
| `$ErrorActionPreference='Stop'` + 原生命令 | `docker build/run` 的进度、gcc 警告都走 stderr，在 Stop 下被当终止错误，构建意外中断 | 改 `Continue`，成败一律看 `$LASTEXITCODE` |
| `Set-Content -Encoding UTF8` | PS 5.1 写出**带 BOM + CRLF**，而 `TOOLCHAIN.txt` 要跟着产物上 Linux/板子 | 用 `[System.IO.File]::WriteAllText` 显式写无 BOM + LF |

### D-6. Fedora 上不需要装 `docker`

`docker` 与 `podman` 二选一即可，构建脚本会自动选择运行时。
Fedora 装 `podman` 就够，不需要额外配 Docker 仓库。

### D-7. `npm` 的全局前缀

与本项目无关，但同机环境上踩过：环境变量 `global-prefix=/usr/local` 会**覆盖**
`~/.npmrc` 里的 `prefix`，所以要免 root 装全局包必须显式覆盖：

```bash
export npm_config_prefix=/home/skywind_fox/.local
npm install -g <pkg>
```

---

## E. 未遇到但值得警惕的

| 现象 | 原因 | 处理 |
|---|---|---|
| 触摸坐标偏移 / X 轴镜像 | 触摸控制器坐标范围与屏幕分辨率不匹配 | `xinput set-prop <id> "Coordinate Transformation Matrix" ...` |
| 屏幕不亮 | 背光 GPIO（PD13 / `LCD_BL`）没使能 | 查设备树 `backlight` 节点 |
| 触摸内核认到但 X 不认 | X server 没加载 evdev/libinput 驱动 | 装 `xserver-xorg-input-libinput`，查 `/var/log/Xorg.0.log` |
| `xdpyinfo: unable to open display ""` | ssh/串口会话里 `DISPLAY` 为空；Xorg 还带 `-auth` | `export DISPLAY=:0`，必要时设 `XAUTHORITY` |
| 系统里没有 `gcc-arm-linux-gnu` | Fedora 不提供这个包名 | 本项目交叉编译走容器，不需要它 |
| 全屏下 34/40 号字观感偏大 | 设计如此（`kBigFontPx` / `kSmallFontPx`），板子 1024×600 | 改 `src/gui.cpp` 里这两个常量 |
| `#include <linux/...>` 交叉编译能用，`<X11/...>` 不能 | 前者是内核 UAPI（Zig 自带 611 个），后者是外部库 | 后者需要 sysroot，见 [B-6](#b-6-zig-不带任何-x11-头文件) |
