# workflow.md — 项目活文档

> 这份文件接替已删除的 `HANDOFF.md`。
> 区别在**时效**：`HANDOFF.md` 是一次性快照（写的时候对应某个提交，几天就过期）；
> 这里只放**不会随提交过期的东西** —— 未决的取舍、界面的约定、以及"真机验证到哪了"。

| 想知道什么 | 去哪 |
|---|---|
| 怎么构建 / 怎么跑 / 板上什么环境 | [README.md](README.md) §3–§5 |
| 踩过的坑、已知问题、未验证项 | [WARNING.md](WARNING.md) |
| 要在 Windows 上构建，或者要上板实测 | [VERIFY-RUNBOOK.md](VERIFY-RUNBOOK.md) |
| 未决设计问题、界面约定、协作上的硬约束 | **本文** |

---

## 1. 真机验证进度

> 更新这一节时**只写实测过的**。没跑过的写"还没做"，不要写"应该没问题"。

| 项 | 状态 | 说明 |
|---|---|---|
| Linux 交叉编译（`build-armhf.sh`） | ✅ 实测通过 | 0 警告；同源同镜像重建产物 sha256 逐字节一致（可复现） |
| Windows 交叉编译（`build-armhf.ps1`） | ✅ 已跑通 | FBI-0537 在 Windows 上实跑（2026-09-19，PR #8）：`-Wall -Wextra` 零警告，产物 407948 / 9788 字节。**待办 #2 达成**（此前只改过、从没执行过） |
| 板上实测（LVGL 版） | ❌ **还没做** | README §10、WARNING A-1；逐项判据见 runbook §3–§4 |
| 原生 GUI（开发机预览） | ✅ 三页正常 | 首页 / LED 调试页 / 蜂鸣器调试页：切页、按钮派发、退出路径都实测过 |
| GPIO 输入（libgpiod） | ⚠ 只有骨架 | 依赖链已配齐并交叉编译通过；**没有任何调用点**，板端也没验过（不知道传感器接在哪个 gpiochip / 哪条线上） |

---

## 2. 未决的设计问题

### 2.1 业务层与 GUI 还没打通

```
main.cpp ──> cpp_start() ──> device_init()  ← 唯一有实现的
                          ──> status()      ← 空壳
                          ──> gui_build()   ← 空壳
                          ──> render()      ← 空壳

gui.cpp  ──> 自己的 main()、自己的 LVGL 循环
```

`gui.cpp` **完全不调用 `cpp_start()`**：`device_init` / `status` / `render` 在 GUI 里是
死代码，GUI 的 `main` 里也没有周期业务逻辑（`fc_tick` 之类）的位置。`start.cpp` 目前
只服务控制台程序 `deffire-dev`。

**要先决定方向**，再往里加东西：

- **A**：GUI 就是应用本体，`start.cpp` 退化成控制台 demo（那 `status`/`render` 的空壳该删）；
- **B**：把 `cpp_start` / `status` / `render` 接进 GUI 的循环（那要先定"周期逻辑多久跑一次、
  界面怎么拿到状态"）。

在这之前**别往 `start.cpp` 里加功能** —— 加了也不知道谁会调用它。

### 2.2 真全屏（等实测再决定）

板上有 Openbox，窗口大概率被加装饰（开发机上实测：请求 1024x600，实际报 1074x687）。
要真铺满得在建窗口后发 `_NET_WM_STATE_FULLSCREEN`（走 EWMH）——**现在没实现**。
先按 runbook §4.3 上板确认有没有被加标题栏，再决定写不写。详见 WARNING C-7。

### 2.3 传感器模块（0%，但有骨架）

代码量还是 0。现在只有一条通道：`useable_tools::gpio_read_value()`（libgpiod v1），
依赖链已配齐 —— `armhf-toolchain/Dockerfile` 里装了 `libgpiod-dev:armhf`（1.6.3，v1），
CMake 用 `WITH_GPIOD`（默认 AUTO）控制，见 README §5.5。

**只支持 v1 API**：`gpiod_chip_open_by_label` / `gpiod_line_request_input` 这套在
**v2 里被删了**。容器与板子（Debian 12）是 1.6.3 = v1，可用；**Fedora 44 是 2.2.5 = v2**，
所以原生 Fedora 构建拿不到这个功能（CMake 会认出版本不符并明确跳过，不会拿一堆
"未声明标识符"糊你一脸）。要在原生环境用它，得先把 `gpio_read_value()` 移植到 v2。

**板上实测（2026-09-19，D-9 修好权限之后，`gpiodetect` / `gpioinfo` 不再 Permission denied）**：

控制器 9 个。`chip_label` 就是 `gpiodetect` **方括号里**那个 —— **`GPIOA` … `GPIOI`**：

| gpiochip | label | 线数 | 备注（实测快照） |
|---|---|---|---|
| 0 / 1 / 2 / 3 | GPIOA / GPIOB / GPIOC / GPIOD | 16 | 绝大多数线被内核占用 |
| **5** | **GPIOF** | 16 | **line 8 = `"beep"`（蜂鸣器）**；line 14 = `"USER-KEY1"`（板上按键） |
| 6 / 7 | GPIOG / GPIOH | 16 / 15 | GPIOH line 5 = `"reset"` |
| **8** | **GPIOI** | 8 | **line 3 = `"sys-led"`（LED）**；0 = `irq`、1 = `spi0 CS0`、2 = `reset` |

⚠ **别把 `gpiochip0` 当 label 填。** 那是**设备名**（`gpiod_chip_open_by_name()` 用的那个），
而 `gpio_read_value()` 走的是 `gpiod_chip_open_by_label()` —— 填 `gpiochip0` 会找不到芯片。

**"LED / 蜂鸣器的引脚归内核持有"现在是实测结论，不是推断了**：`gpioinfo` 里那两条线的
consumer 就是 `"sys-led"` / `"beep"`，状态 `[used]`。所以它们**必须**继续走 sysfs，
用 libgpiod 去 request 会失败（EBUSY）。两者 DT 极性都是 **active-low**，但这不影响我们
写 sysfs（`brightness=1` 就是"亮 / 响"）。

**传感器接在哪条线上，仍是硬件事实 —— `gpioinfo` 给不了。** 它只能告诉你"哪些线空闲"，
而"空闲"不等于"接了什么"。所以 `src/start.cpp` 的接线表保持空白，等接线确认后一行填一行。
下面是当天的空闲线快照（仅供对照，**别拿它当接线依据**）：

```
GPIOA: 6, 11, 13(output), 14      GPIOB: 10          GPIOC: 14, 15
GPIOF: 15                         GPIOH: 0, 1, 4(output)      GPIOI: 4, 5, 6, 7
GPIOD / GPIOE / GPIOG: 全部被占用
```

板端还要有运行时库：`sudo apt install libgpiod2`。**不装整个界面起不来**（不是局部功能失效），
因为交叉产物链着 `libgpiod.so.2`；`verify-on-board.sh` 会把缺失的库列出来。

### 2.4 GPIO **输出**（外部无源蜂鸣器，2026-09-19 随 PR #8 加的）

板上 PA6（= **GPIOA line 6**，实测空闲）接了一个**无源**蜂鸣器模块，首页有入口、
独立调试页是 开 / 关 / 返回（**没有"心跳"** —— 它不归内核 leds-gpio 管，没有 `trigger` 文件）。

写 GPIO 有两条路，**别用错**：

| 用途 | 用什么 |
|---|---|
| 偶尔写一次电平 | `useable_tools::gpio_write_value()` —— 内部走一整轮 open/request/release/close |
| **反复翻转**（方波、PWM 模拟） | `useable_tools::gpio_output_open()` 拿手柄，循环里只 `gpio_output_set()`，用完 `gpio_output_close()` |

**教训（PR #8 里出现过的写法）**：每个脉冲都调 `gpio_write_value()`，而主循环每轮跑一次
（~900 次/秒）⇒ ~1800 次/秒的芯片开关，板上单核会被拖慢；而且脉冲之间线是**放开**的，
引脚没有持续驱动 —— 现象就是"一连串咔哒声，不是音调"。

**发声不能挂在主循环上**：挂在上面时每轮最多翻一次电平，音调 = 循环速率/2（开发机 ≈450 Hz，
板上更慢 —— 实测"音调很低"）。现在 `buzzer_out_set(true/false)` 只置一个 `atomic` 标志，
**专用线程**用 `clock_nanosleep` 的**绝对时刻**（`TIMER_ABSTIME`）翻转 GPIOA:6，频率与
主循环彻底无关。频率默认 **2 kHz**，可在板上运行时试：

```bash
FIRECONTROL_BUZZER_HZ=3000 DISPLAY=:0 ./deffire-gui-dev     # 100..8000，超范围会提示并回落
```

试出这个蜂鸣器最响的频率后，把 `kOutBuzzerDefaultHz` 改成它（小无源蜂鸣器的谐振点通常在
2~4 kHz）。**开发机上实测**：默认 2 kHz 时线程每秒约 4028 次翻转（≈2014 Hz），主循环仍
909 次/秒不受影响；线程占单核约 3%（那是"写值是空操作"时的开销，板上每次多一次 ioctl）。

想要**零 CPU**、更准的音调仍可走内核 PWM（`/sys/class/pwm`），但那要把 PA6 复用成 PWM
通道并上板验证 —— 当前先用 GPIO + 线程这套。

**还没实测**：板上那个蜂鸣器"能响"已确认，但**最响的频率**没试过（用上面的环境变量扫一遍）。

**一个副作用（知道就好，暂时不用管）**：`gpio_read_value()` 与 `write_File()` 在同一个
翻译单元（`usetools.cpp`），链接器为 `write_File` 拉进这个 `.o` 时会把 gpiod 引用一起带上
—— 于是**连不读 GPIO 的控制台程序 `deffire-dev` 也依赖 `libgpiod.so.2`**
（产物 5,648 → 9,768 字节）。想去掉这个连带依赖，把 `gpio_read_value()` 挪到自己的
`.cpp`（如 `src/gpio.cpp`）并登记进 `CMakeLists.txt` 即可，那样只有真正调用它的目标才链
libgpiod。**现在没做** —— 板子无论如何都要装 `libgpiod2`（GUI 需要），收益只是让控制台
程序少一个依赖。

`~/下载/` 里那份官方 `main.c` 用的是"综合例程扩展板"的 GPIO 配置（3 个 LED、蜂鸣器走
`EV_SND`），**和手上这块底板不是一回事** —— 可以看它怎么调 libgpiod，但引脚编号别照抄。

---

## 3. 界面的约定（改 `src/gui.cpp` 之前先读）

界面是**单屏 + 分页底栏**：顶栏（品牌 / 状态灯 / 退出）三页完全一致，切页只重建底栏按钮。

1. **`Action::Nothing` 必须留在枚举第一位。** `ButtonSpec` 是聚合类型，表里漏写 `action`
   字段时它会被值初始化为 `0`；第一位是 `Nothing` 就退化成"占位"（点了没反应），
   第一位是别的（比如 `LedOn`）就会**静默点亮 LED**。
2. **`onAction()` 的 `switch` 不写 `default`。** 往 `Action` 加成员忘了加 `case` 时，
   `-Wswitch` 会在编译期报出来。别为了消警告去加 `default`（要消 `-Wformat-overflow`
   就给结果变量一个非空初值）。
3. **加页面 / 加按钮只改表**：`kHomeButtons` / `kLedButtons` / `kBuzzerButtons` + `kPages`。
   数组长度用 `countOf()` 取，**不要手写数字** —— 手写的 `kNumButtons` 和表对不上，
   已经坏过一次（越界初始化）。
   **"每行放几个"是每页自己的参数**（`PageSpec::columns`，首页 3 列、调试页 4 列），
   不再是全局常量；行数由 `rowsOf(count, columns)` 算。末行不满时 `buildPageButtons()`
   会补**透明占位**，让各行按钮等宽（不补的话 `flex_grow` 会把末行少量按钮撑成半屏宽）。
   `columns` 必须 ≥ 1（有 `static_assert` 挡住除零）。
4. **`Page` 枚举的顺序必须与 `kPages` 一致**（有 `static_assert` 兜着）。
5. **切页只置 `g_page_dirty`，重建在主循环里做。** 不要在事件回调里删掉正在派发的那个
   按钮所在的底栏。
6. **控制台那行 `点击【x】坐标 (x,y) → 结果` 的格式不要改。** 板上核对触摸命中、
   记录硬件响应全靠它（runbook §4.2 的验收表按这个格式写的）。
7. `kText = "halloworld"` 是**有意保留**的（WARNING A-3）。内容区那行小字显示"页名 ·
   上一步结果"，也别删 —— 板上不接控制台时它是唯一的操作反馈。
8. `q` / `Esc` 在每一页都是退出程序（板子是触摸屏，键盘只给开发机用）。

---

## 4. 协作上的硬约束

- **远程用 SSH，不要用 HTTPS。** `git@github.com:Skywindfox/DefFireWork.git` ——
  `github.com` 的 HTTPS 时通时断，推一半失败很难查。
- **开发机跑不了 armhf 产物**（架构不同 + 需要板上的 libX11），必须 scp 到板上跑。
- **板上不编译**：一切从开发机交叉编译后推过去，产物统一放板上 `~/Desktop`。
- **`~/下载/` 里的正点原子资料别照抄**：`main.c` 是"综合例程扩展板"的配置（3 个 LED、
  蜂鸣器走 `EV_SND`），**不是手上这块底板**（1 个 LED + 1 个蜂鸣器，都在
  `/sys/class/leds/` 下）；那份 PDF 讲的是出厂 Buildroot 系统，指令不能直接搬到 Debian。
  IIO 的 `iio:deviceN` 编号是动态分配的，**别写死**。
