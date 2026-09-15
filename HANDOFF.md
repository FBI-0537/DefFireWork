# FireControlApp 交接文档

> 写于 2026-09-13，对应提交 `9a81c2a`。
> 本文档用于**跨对话/跨人交接**——读完这一份就能接手，不必回溯历史对话。
> 持久的技术细节在 [WARNING.md](WARNING.md)（踩坑总集）和 [README.md](README.md)（使用说明），
> 本文只做**现状、结论、待办**的汇总并指向它们。

---

## 1. 项目与协作

| 项 | 值 |
|---|---|
| 项目 | 省级大创：智能楼宇消防系统（FireControlApp） |
| 仓库 | `git@github.com:Skywindfox/DefFireWork.git` |
| 推送方式 | **SSH**（`github.com` 的 HTTPS 时通时断，不要用 HTTPS 远程） |
| 成员 | Skywindfox（Fedora 开发机）、FBI-0537（Windows，负责 LVGL 界面） |
| 板端用户 | `fbi`（在 `sudo`、`users` 组） |
| 板端工作目录 | **`~/Desktop`**（团队约定，所有产物与脚本都放这里） |

---

## 2. 目标板：正点原子 ATK-DLMP135

```
OS      : Debian 12 bookworm armv7l（自编译）
Kernel  : 5.15.24
显示    : Xorg :0
桌面    : DE: LXDE    WM: Openbox    ← **有窗口管理器**（早期误判为"没有"）
屏幕    : 1024x600 电容触摸屏
CPU     : Cortex-A7 单核 1GHz
内存    : 437 MiB（选型硬约束）
```

**板上不编译**（性能不够）。一切都从开发机交叉编译后推过去。

---

## 3. 维护者开发机

| 项 | 值 |
|---|---|
| 系统 | Fedora |
| 容器 | **podman**（没装 docker） |
| 镜像源 | `docker.io` 不可达 → 用 `docker.m.daocloud.io/library/debian:bookworm-slim` |
| `pwsh` | **没有** → `build-armhf.ps1` 无法在本机验证 |

交叉编译环境封装在 `armhf-toolchain/`（Docker，base 与板子同为 bookworm，避免 glibc 错配）。

---

## 4. 当前仓库状态

`main = origin/main = 9a81c2a`，工作区干净，**开放 PR / issue 均为 0**。

```
9a81c2a  自检修复: 静态误报与产物缺失被静默忽略
6ff013e  产物改名 deffire-*-dev, 板端部署目录统一到 ~/Desktop
0015bbd  Merge PR #5: 界面深色工业风改版
0b8d30d  开/关 LED 与蜂鸣器前先清 trigger, 否则被心跳覆盖
163977f  修复 issue #4 的遗留项: 清理旧产物目录 + .vscode/tasks.json
6845317  同步板上实况, 并补部署与权限工具
```

### 4.1 构建目标

| 目标 | 源文件 | 说明 |
|---|---|---|
| `firecontrol`（静态库） | `src/greeting.cpp` `src/usetools.cpp` `src/start.cpp` | 纯逻辑层，可交叉编译 |
| `deffire-dev` | `src/main.cpp` | 控制台程序，打印 `hallo world` |
| **`deffire-gui-dev`** | `src/gui.cpp` | **主程序**：LVGL v9.2.3 + X11 + FreeType |
| `test_greeting` | `tests/test_greeting.cpp` | 单元测试（`ctest`） |

`-dev` 后缀表示当前是开发阶段构建，正式版去掉。

### 4.2 脚本

| 脚本 | 作用 |
|---|---|
| `build.sh` | 总入口：`native` / `gui-full` / `gui-armhf` / `all` / `clean`<br>
| `armhf-toolchain/build-armhf.sh` | **Docker 里交叉编译（armhf GUI 唯一正确路径）** |
| `armhf-toolchain/build-armhf.ps1` | 同上，Windows 用（**从未实际执行过**） |
| `armhf-toolchain/deploy-to-board.sh` | 传产物到 `~/Desktop` + 探测 sysfs 写权限 |
| `armhf-toolchain/setup-board-permissions.sh` | **板上跑一次**：udev 规则解决 LED/蜂鸣器权限 |
| `armhf-toolchain/verify-on-board.sh` | 校验产物与本板匹配（ABI + 运行时依赖） |

### 4.3 目录结构

```
src/
  main.cpp          控制台入口：只调用 cpp_start()
  start.cpp/.h      应用层骨架：device_init / status / gui_build / render
                    + 6 个板载硬件控制函数（LED / 蜂鸣器）
  gui.cpp           LVGL 界面（自带 main，独立于 start.cpp）
  hardware_path.h   所有 sysfs 设备路径（inline constexpr）
  usetools.cpp/.h   read_File（读浮点数）/ write_File（覆盖写）
  greeting.cpp/.h   示例模块，被单元测试覆盖
third_party/lvgl/   LVGL v9.2.3（已裁剪，~16MB，随仓库提交）
armhf-toolchain/    Docker 交叉编译环境 + 板端脚本
```

---

## 5. 硬件事实

### 5.1 已确认

| 项 | 结论 |
|---|---|
| LED | `/sys/class/leds/sys-led/brightness`，`"1"` 亮 / `"0"` 灭 |
| 蜂鸣器 | `/sys/class/leds/beep/brightness`，`"1"` 响 / `"0"` 停（**active-high**） |
| trigger | **独立的 sysfs 文件** `/sys/class/leds/<name>/trigger`，两者**都支持** `heartbeat` |
| trigger 语义 | **trigger 生效时内核周期性覆盖 brightness** → 改亮度前必须先写 `trigger="none"` |
| sysfs 权限 | 默认 `root:root`，普通用户只读 → 必须 udev 规则（`leds` 组），**须重启生效** |
| `sys-led/invert` | 存在。极性反了改它，**不要改代码里的 `"1"`/`"0"`** |

以上来源：《ATK-DLMP135 功能测试》4.1 + 板上实测（`ls -l /sys/class/leds/`）。

### 5.3 参考资料（`~/下载/`）

| 文件 | 说明 |
|---|---|
| `01【正点原子】ATK-DLMP135快速体验V1.1.pdf` | 出厂 **Buildroot** 系统的测试手册，74 页。**指令不能照抄到 Debian**（内核/设备树可能不同） |
| `main.c` | 正点原子**官方综合示例**（温湿度/光照/电流电压 IIO + GPIO 火焰/人体/光电 + MQTT）。**是"综合例程扩展板"的配置**（3 个 LED；蜂鸣器走 `/dev/input/by-path/platform-beeper-event` + `EV_SND`），**不是你手上这块底板**（只有 1 LED + 1 蜂鸣器）。`usetools` 的 `read_File`/`write_File` 出自它，但质量差：返回值被忽略、`fprintf(fp, data)` 把数据当格式串 |

**IIO 的 `iio:deviceN` 编号是动态分配的，别写死。**

---

## 6. 架构现状（有个缺口）

```
main.cpp ──> cpp_start() ──> device_init()  ← 唯一有实现的（打印问候语）
                          ──> status()      ← TODO 空壳
                          ──> gui_build()   ← TODO 空壳
                          ──> render()      ← TODO 空壳

gui.cpp  ──> 自己的 main()，自己初始化 LVGL、自己跑循环、深色界面 + 8 按钮
```

**缺口：`gui.cpp` 完全不调用 `cpp_start()`。**
应用层骨架（`device_init`/`status`/`render`）在 GUI 里是**死代码**；`gui.cpp` 的
`main` 里也没有周期业务逻辑（`fc_tick` 之类）的位置。**业务与界面尚未打通**，
这是下一步的主要设计点。

### 6.1 界面

8 个按钮（`kButtons`），底栏 2 行 × 4（`kPerRow = 4`）：

```
上排: LED 开    LED 关    LED 心跳    蜂鸣器开
下排: 蜂鸣器关  蜂鸣器心跳  功能 8(占位)
右上: 退出（顶栏右侧）
```

派发用 `Action` 枚举 + `switch`，**故意不写 `default`** —— 以后往 `Action` 加成员
忘了加 case 会由 `-Wswitch` 在编译期报出来。

> 坑：枚举成员叫 `Action::Nothing` 而不是 `None`。`Xlib.h` 有 `#define None 0L`，
> 会把 `Action::None` 顶成数字常量。

**界面中央大字 `kText = "halloworld"` 有意保留未改**（程序已改名 `deffire-gui-dev`，
文字不动是明确的决定）。要改只动 `src/gui.cpp` 那一个常量。

关键常量：`kBigFontPx = 40`（主标题）、`kSmallFontPx = 18`（顶栏 / 按钮标签）、
`kStatusFontPx = 16`（状态说明这类次要小字）、`kNumButtons = 8`、`kPerRow = 4`。

---

## 7. 待办

| # | 事项 | 说明 |
|---|---|---|
| 1 | **上板验证一切** | 见 §5.2。**优先级最高，其他都排在它后面** |
| 2 | `build-armhf.ps1` 首次运行 | 唯一改过但**从未执行**的代码 |
| 3 | 业务层与 GUI 打通 | `gui.cpp` 不调 `cpp_start()`；`status`/`render` 是空壳 |
| 4 | 真全屏 | 板上有 Openbox，窗口大概率带标题栏。需发 `_NET_WM_STATE_FULLSCREEN`（EWMH）。**先上板确认再决定写不写** |
| 5 | 传感器模块 | 0%。IIO / GPIO 读数还没碰 |
| 6 | `clear_heartbeat` 未接按钮 | `led_onboard_clear_heartbeat()` / `buzzer_...` 零调用。|
| 7 | 底栏两行宽度不等 | 第二行 3 个按钮约 328px，第一行 4 个约 243px（外观问题） |
| 8 | ~~清理已删构建模式的文档引用~~ | ✅ **已完成**（见 §11）。Zig 路径删除后遗留的文档/配置不一致已全部清理，顺带修掉 `build.sh` 的参数转发 bug |

---

## 8. 约定与教训

> 以下几条是踩坑换来的，重复犯错成本很高。

1. **不要凭推断断定硬件行为。** 翻过两次车：`/dev/fb0` 归 Xorg 推不出"没有 WM"
   （实际有 Openbox）；蜂鸣器挂在 `gpio-leds` 上推不出"低电平触发"（实际 active-high）。
   **要么查文档，要么板上实测；不确定就写"待确认"并给出验证命令。**

2. **失败必须显式暴露，绝不静默跳过。** 同类问题出现过三次：验证脚本缺 `readelf` 时
   "静默通过"（WARNING D-2）；取产物失败被吞（GUI 缺失但控制台在，构建仍算成功）；
   动态依赖检查依赖 Zig 包装脚本，没有它的机器上整段静默跳过。
   **检查不到东西时要报错，不是不吭声。**

3. **判断某个东西是不是遗留，查 `git log -S`**，不要看当前代码里有没有人用。

4. **改 `armhf-toolchain/build-armhf.ps1` 后必须确认 UTF-8 BOM 还在**：
   `head -c3 ... | od -An -tx1` 应为 `ef bb bf`。
   **编辑工具会静默吃掉 BOM**（已发生两次），PS 5.1 下会按 ANSI 读中文导致语法崩溃。
   改这个文件建议用 `sed`。

5. **零警告**：`-Wall -Wextra`；第三方 LVGL 用 `-w` 豁免。

6. **`sudo echo 1 > file` 不管用**（重定向由 shell 执行，shell 还是普通用户），
   必须 `sudo sh -c '...'` 或 `sudo tee`。

8. **改动前先问，别自己扩范围。** 曾有"顺手多做"被明确纠正。

---

## 9. 文档索引

| 文件 | 内容 |
|---|---|
| `README.md` | 使用说明。§5 构建/部署、§10 待办 |
| **`WARNING.md`** | **踩坑总集**：A 当前遗留 / B 交叉编译（18 条）/ C 界面字体（9 条）/ D 环境工具（8 条）/ E 值得警惕 |
| `armhf-toolchain/README.md` | 工具链细节、为什么用 COPY 而不是挂载 |
| 本文档 | 现状、结论、待办汇总 |

**接手时建议先读** `WARNING.md` 的 A 节与 C 节，以及 `README.md` §5。

---

## 10. 下一步怎么走

### 10.1 等两件事的结果

1. Windows 上 `.\armhf-toolchain\build-armhf.ps1` 是否跑通
2. 板上实测结果

### 10.2 构建（Windows）

```powershell
git pull
.\armhf-toolchain\build-armhf.ps1
```

产物在 `build-armhf\`：`deffire-gui-dev`、`deffire-dev`、`TOOLCHAIN.txt`。

`docker.io` 拉不动时：

```powershell
.\armhf-toolchain\build-armhf.ps1 -BaseImage docker.m.daocloud.io/library/debian:bookworm-slim
```

### 10.3 部署到板子

```powershell
$B = "fbi@<板子IP>"
scp build-armhf\deffire-gui-dev build-armhf\TOOLCHAIN.txt armhf-toolchain\setup-board-permissions.sh "${B}:~/Desktop/"
```

板上**第一次**（只需一次）：

```bash
sudo bash ~/Desktop/setup-board-permissions.sh fbi
sudo reboot
```

运行：

```bash
cd ~/Desktop && DISPLAY=:0 ./deffire-gui-dev
```

### 10.4 上板重点看这几条

2. LED / 蜂鸣器 6 个按钮逐个点，确认实际响应。
   蜂鸣器权限没配好会"点了没反应"，`setup-board-permissions.sh` 就是解决它的。
3. 深色底上文字对比度、按下态是否"立刻有反应"。
4. 触摸命中是否准确、坐标是否偏移（控制台会打印点击坐标）。
5. **窗口是否真全屏** —— 有 Openbox，很可能带标题栏/留边距。
   若没铺满，再考虑实现 `_NET_WM_STATE_FULLSCREEN`（现在**没实现**）。

---

## 11. 本轮重构与整理（**已提交并推送**）

> 这一节记录交接前那次「删除 Zig 交叉编译路径」重构及其跟随整理。
> **已经全部提交推送**，工作区是干净的 —— 不要去找未提交的改动。

交接时的提交序列（`main = origin/main = 93bf1d1`）：

```
93bf1d1  build-armhf.sh: 取产物前先清旧文件, 否则逐个检查会被旧产物骗过
1e51a76  Zig 路径删除后的文档与配置跟随清理
9c470dc  删除 Zig 交叉编译路径; 新增交接文档 HANDOFF.md
281eb89  Merge pull request #6 (FBI-0537: build-armhf.ps1 补逐个产物检查 + 清旧产物)
```

| 文件 | 改动 |
|---|---|
| `build.sh` | ①删除 Zig 旧路径（去 `run`/`gui`/`armhf`/`verify` 四个模式，删 `TOOLCHAIN`、`need_armhf()`、`do_armhf()`、`verify_armhf()`、`export LC_ALL=C`，`do_gui`→`do_gui_full`）<br>②**修参数转发 bug**（见下） |
| `src/start.cpp` | 仅行尾空格 + 一处注释空格 |
| `workflow.md` | 仍有 1 行内容「不需要这个。」（维护者留下的备注，未删） |
| `README.md` | 加 HANDOFF 链接；模式清单改为实际 5 个；Zig 段改为「已废弃」；目录树/产物说明 |
| `WARNING.md` | 字号笔误（34 → 40/18/16）；A-5 改为「`build.sh` 没有运行模式」；新增 B-19 |
| `.vscode/tasks.json` | 删除用已废弃 Zig 工具链的孤儿任务（10 → 9 个） |
| `CMakeLists.txt` | 2 处注释标注该工具链已废弃；FATAL_ERROR 不再建议用它 |
| `armhf-toolchain/README.md` | 不再说 Zig 工具链「仍然可用」 |
| `armhf-toolchain/build-armhf.sh` | 取产物前先清旧同名文件（见下） |
| `HANDOFF.md` | 新建 |

### 修掉的两个 bug（都是"静默给出错误结果"这一类）

**1. `build.sh` 参数转发静默失效**

`do_gui_armhf()` 里有 `"$script" "$@"`，看起来是把参数转给
`armhf-toolchain/build-armhf.sh`，但 case 分支调用时没传参：

```sh
gui-armhf) do_gui_armhf ;;          # 函数内 $@ 为空 → "$@" 展开成零个词
```

后果：`./build.sh gui-armhf --rebuild` 里的 `--rebuild` 被**静默丢弃**，
命令照常跑完、退出码 0，看起来像"重建过了"其实没有。

**已修**：改为 `gui-armhf) do_gui_armhf "${@:2}" ;;`，现在 `--rebuild` / `--shell`
都能真正转发。实测 `./build.sh gui-armhf --bogus` 会被底层脚本以「未知参数: --bogus」
拒绝并返回 1（修复前是静默通过）。

顺带补上了 `build.sh` 缺失的文件末尾换行。

**2. `build-armhf.sh` 取产物前不清旧文件 → 逐个检查被旧产物骗过**

这个 bug 是**看了 FBI-0537 的 PR #6 才发现的** —— 他在 `build-armhf.ps1` 里加了
「构建前清旧产物」，同样的道理适用于 `.sh`：

```
podman cp 失败时不会删除目标文件, 上一次的同名产物留在原地
  → "逐个检查产物是否存在"看到的是旧文件 → 判定成功
  → 容器里 GUI 编失败(只有控制台产出) 会被当成构建成功, 交付一个陈旧的界面
```

只把「至少有一个产物」改成「逐个检查」**并不足够**，因为旧文件仍然满足检查。
**已修**：取产物之前先删掉旧的同名文件（删不掉就直接失败），并把产物清单收敛成
`ARTIFACTS` 数组，清旧 / 取回 / 检查三处共用一份。见 `WARNING.md` B-19。

### 该重构遗留的不一致：**已清理**

| 位置 | 处理 |
|---|---|
| `README.md` 的模式清单 | 改为实际的 5 个模式；补 `--rebuild`/`--shell` 用法 |
| `README.md` 的 Zig 章节 | 改为「旧路径：Zig（已废弃）」 |
| `README.md` 目录树 / 产物说明 | 标注 `toolchain-armhf.cmake` 已废弃；产物只来自容器 |
| `WARNING.md:24` | 改为「`build.sh` 没有运行模式」 |
| `.vscode/tasks.json` | **删除**「CMake: 构建 armhf」任务（直接用已废弃的 Zig 工具链，且与「构建 GUI armhf」重复）；任务数 10 → 9 |
| `CMakeLists.txt` | 两处注释改为标注该工具链已废弃；FATAL_ERROR 提示不再建议用它 |
| `armhf-toolchain/README.md` | 不再说它「仍然可用」，改为已废弃 |

**保留未删**：`cmake/toolchain-armhf.cmake` 文件本身，以及 `WARNING.md` 的 B 节
（B-1…B-15 全是 Zig 时代的坑）。它们不再被任何脚本引用，但**那 15 条是换来的经验**，
删了可惜；要删请单独决定。

**删除 `export LC_ALL=C` 是安全的**：`build.sh` 现在不再解析 `readelf` 输出
（那段已移入 `armhf-toolchain/build-armhf.sh`，它自己内部设了 `LC_ALL=C`）。

### 事后看：那次提交混了三件事

实际的提交 `9c470dc` 把「Zig 路径删除」+「`build.sh` bug 修复」+「新增交接文档」
塞进了同一个提交（原提交信息只有一个字 `1`，后来 amend 成描述性信息）。
内容没错，但拆成三个提交会更好回溯。

教训：**别 `git add -A` 一把梭** —— 尤其在工作区可能混有别人在途改动的时候。


