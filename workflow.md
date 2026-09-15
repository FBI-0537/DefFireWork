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
| Windows 交叉编译（`build-armhf.ps1`） | ❌ **从未执行过** | 开发机没有 `pwsh`；步骤见 runbook §1 |
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
依赖链已配齐 —— `armhf-toolchain/Dockerfile` 里装了 `libgpiod-dev:armhf`，
CMake 用 `WITH_GPIOD`（默认 AUTO）控制，见 README §5.5。

**动手前必须先确认（现在全是未知）**：

1. **传感器到底接在哪个 gpiochip、哪几条线上？** 用 `gpiodetect` / `gpioinfo` 在板上查。
2. **那些线是不是已经被内核占用了？** `gpioinfo` 里看 "used by"。
   板载 LED / 蜂鸣器已经归 `leds-gpio` 驱动持有，再 `request` 会失败（EBUSY）——
   这也是为什么它们**必须**继续走 sysfs，而不是 libgpiod。
   ⚠ 这条是推断，按"不要凭推断断定硬件行为"的规矩：**上板用 `gpioinfo` 实测确认**。
3. 板端要装运行时库：`sudo apt install libgpiod2`。**不装整个界面起不来**（不是局部功能失效），
   因为交叉产物链着 `libgpiod.so.2`；`verify-on-board.sh` 会把缺失的库列出来。

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
