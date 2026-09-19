#include "start.h"

#include <cstdio>
#include <unistd.h>  // usleep

#include "greeting.h"
#include "hardware_path.h"
#include "usetools.h"

// ---------------------------------------------------------------------------
// 项目的主要内容都在本文件。
//
// main.cpp 只做一件事: 调用 cpp_start()。这样入口文件保持简单, 真正的逻辑
// 都集中在这里, 加功能不用往 main.cpp 里塞东西。
//
// 关于"要不要循环" (原来写在这里的疑问):
//   这里**不写循环**。cpp_start() 是"启动一次, 然后返回"的位置。
//   循环留给 main 或 GUI 的主循环 —— LVGL 的 lv_timer_handler() 必须被反复
//   调用, 而且两次之间的间隔要按它的返回值来睡, 所以它只能待在真正的主循环
//   里。详见 src/gui.cpp 的 main(), 那是现成的例子。
// ---------------------------------------------------------------------------

// 各阶段做什么, 见下面每个函数的注释。
// 定义在本文件后面, 但 cpp_start() 在上面就要用 —— C++ 必须先声明后使用,
// 少了这几行会报 'device_init' was not declared in this scope。

int device_init();  
int status();       
int gui_build();    
int render();       

int cpp_start()
{
    if (device_init() < 0) {
        std::printf("初始化失败, 退出\n");
        return 1;
    }

    status();
    gui_build();
    render();

    return 0;
}

// ---------------------------------------------------------------------------
// 初始化: 开设备、读配置、建数据。
// 返回值: 0 成功, 负数表示失败(由 cpp_start 决定要不要继续)。
// ---------------------------------------------------------------------------
int device_init()
{
    std::printf("%s\n", firecontrol::greeting().c_str());

    // TODO: 打开传感器 / 读配置文件 / 初始化数据结构。

    return 0;
}

// ---------------------------------------------------------------------------
// 板载 GPIO 开关量输入 (传感器)
//
// 和本文件里 LED / 蜂鸣器的区别: 那些是**输出**(往 sysfs 写), 这里是**输入**
// (读 GPIO 电平), 走 libgpiod —— 见 useable_tools::gpio_read_value() 与
// workflow.md §2.3。
//
// 接法当"数据"处理: 全写在下面这张表里, 换传感器 / 换引脚只改表, 逻辑不动。
//
// ⚠ **这张表现在是空的, 而且是故意的。** 这块板子上到底接了哪些开关量输入、
//   接在哪个 GPIO 控制器的哪条线上, 还没确认。**不预置任何"看起来合理"的传感器
//   名** —— 一个猜出来的名字会让人以为那就是需求, 比空着糟得多。
//
//   怎么填 (板上实测, 见 workflow.md §2.3 — 那里有 2026-09-19 的线上表):
//       sudo apt install gpiod
//       gpiodetect      # 有哪些控制器; **方括号里那个才是 chip_label** (如 GPIOA)
//       gpioinfo        # 每条线的编号; consumer 列是"谁占着", unused 才是空闲
//   一行填一个, 字段写全, 例如:
//       {"烟感", "GPIOA", 12, "firecontrol-smoke"},   // 填 label, 不是 "gpiochip0"
//   填完在板上跑控制台程序验证 (它会走 cpp_start() → status()):
//       cd ~/Desktop && ./deffire-dev
//
// ⚠ 填了名字但 chip_label 还是 nullptr 的行会被明确跳过并报"引脚未实测", 不会拿
//   一个猜出来的引脚去读 —— 那种"读回来是 0"最容易被当成"没有火警"。
//
// ⚠ 电平极性(高电平代表"检测到"还是"没检测到")同样要实测确认, 所以这里只返回
//   原始电平 0/1, **不做** "1 == 报警" 这种假设。极性搞反会让"有人"显示成"没人",
//   比读不到更危险。
//
// ⚠ 板载 LED / 蜂鸣器的线已经归内核 leds-gpio 驱动持有 —— 板上实测:
//   sys-led = GPIOI:3、beep = GPIOF:8, `gpioinfo` 里 consumer 就是这两个名字、
//   状态 [used]。所以这里再 request 会失败(EBUSY), 它们继续走 sysfs, 别挪进这张表。
// ---------------------------------------------------------------------------
namespace {

struct GpioInput {
    const char *name;        // 传感器名, 打印用
    const char *chip_label;  // GPIO 控制器的 label (gpiodetect 查); nullptr = 还没实测
    unsigned int line;       // 该控制器内的线号 (gpioinfo 查)
    const char *consumer;    // 传给内核的消费者名, gpioinfo 的 "used by" 会显示它
};

// 接线表: **填一行读一行**; name == nullptr 的行(占位 / 没填)自动跳过。
// 现在只留一个占位行 —— 不预置猜出来的传感器, 理由见上面的说明。
constexpr GpioInput kGpioInputs[] = {
    {},  // 占位: name == nullptr, 会被跳过
};

constexpr int kGpioInputSlots =
    static_cast<int>(sizeof(kGpioInputs) / sizeof(kGpioInputs[0]));

// 表里已经填了几路 (name != nullptr)
constexpr int countFilledInputs()
{
    int n = 0;
    for (const GpioInput &in : kGpioInputs) {
        if (in.name != nullptr) {
            n++;
        }
    }
    return n;
}

// 读一路传感器。
// 返回值: 0 成功(电平写进 out_level); -1 失败(引脚未实测 / chip 打不开 / 线被占用)。
// 失败原因直接打到 stderr —— 读不到就是读不到, 不返回一个"看起来正常"的 0。
int gpio_input_read(const GpioInput &in, int *out_level)
{
    if (out_level == nullptr) {
        return -1;
    }

    if (in.chip_label == nullptr) {
        std::fprintf(stderr,
                     "传感器【%s】的引脚还没实测: 先在板上跑 gpiodetect / gpioinfo, "
                     "把 chip_label 和 line 填进 start.cpp 的 kGpioInputs\n",
                     in.name);
        return -1;
    }

    const int level = useable_tools::gpio_read_value(in.chip_label, in.line, in.consumer);
    if (level < 0) {
        // 具体原因(chip 打不开 / 线被占用 / 没编入 libgpiod)由 gpio_read_value
        // 内部逐条报告; 这里补一句"是哪一路", 免得日志里分不清。
        std::fprintf(stderr, "传感器【%s】读取失败 (%s:%u)\n", in.name, in.chip_label,
                     in.line);
        return -1;
    }

    *out_level = level;
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 状态采集: 读一次当前状态, 供 gui_build() 和 render() 使用。
// 将来的周期采集也应该走这里, 保证"状态怎么来的"只有一处。
//
// 现在做的是: 把 kGpioInputs 里每一路传感器读一遍并打印出来。
// 返回值: 0 —— 单路失败不算整体失败(原因逐条打在 stderr); 负数留给将来
// "整个采集流程都起不来"那种情况。
// ---------------------------------------------------------------------------
int status()
{
    std::printf("--- 板载开关量输入 ---\n");

    const int filled = countFilledInputs();
    if (filled == 0) {
        // 空表不是错误, 是"还没接线 / 还没实测" —— 说清下一步做什么,
        // 不要假装读过了。
        std::printf("  还没有确认的接线: 先在板上跑 gpiodetect / gpioinfo,\n");
        std::printf("  再往 start.cpp 的 kGpioInputs 里一行填一个传感器\n");
        return 0;
    }

    int failed = 0;
    for (int i = 0; i < kGpioInputSlots; i++) {
        const GpioInput &in = kGpioInputs[i];
        if (in.name == nullptr) {
            continue;  // 还没填的行
        }
        int level = -1;
        if (gpio_input_read(in, &level) == 0) {
            std::printf("  %s (%s:%u) 电平 %d\n", in.name, in.chip_label, in.line, level);
        } else {
            failed++;
        }
    }

    if (failed > 0) {
        std::printf("  %d/%d 路没读到 (原因见上面的 stderr)\n", failed, filled);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// 界面搭建: 创建窗口/控件, 属于"只做一次"的初始化。
// 注意不要在这里做绘制 —— 绘制会被反复调用, 建对象只会调一次。
// ---------------------------------------------------------------------------
int gui_build()
{
    // TODO: 建界面 (LVGL 控件、或者控制台的输出格式)。

    return 0;
}

// ---------------------------------------------------------------------------
// 刷新: 把当前状态画出来。会被主循环反复调用。
//
// 所以这里**不能**放"开灯 / 关灯"这种一次性的动作 —— 每帧都会执行一遍。
// 按钮动作要放到按钮的回调里 (见下面 led_on / led_off)。
// ---------------------------------------------------------------------------
int render()
{
    // TODO: 按 status() 得到的状态更新显示。

    return 0;
}

// ---------------------------------------------------------------------------
// LED / 蜂鸣器的开与关
//
// 这两个是"动作", 不是"渲染": 点一次做一次。按钮回调里直接调它们。
//
// 正点原子的 sys-led / beep: 写 "1" 亮(响), "0" 灭(停)。
// write_File 会自动补换行并检查落盘, 所以传 "1" 就行, 不用加 "\n"。
//
// 返回值交给调用者处理: 开发机上这个路径不存在, 会返回 -1,
// 所以失败是常态, 不要在这里 printf 刷屏。
//
// 需要注意的是，这些硬件都是主板原有的硬件，所以加上_onboard_，以区分。
//
// ---------------------------------------------------------------------------
// **先写 trigger="none", 再写 brightness —— 顺序不能反**
//
// trigger 一旦生效 (比如 heartbeat), 内核就接管了 brightness, 会周期性把它改回
// 自己的值。这时手动写 brightness 立刻被覆盖, 现象是"有了心跳之后, 开/关点了
// 完全没反应", 很容易误判成硬件坏了或者权限不对。
//
// 所以每次调亮度之前先把控制权交还给 brightness。
//
// 交还这一步的返回值**故意不检查**: 不是所有设备都提供 trigger 文件, 没有它的
// 时候 brightness 本来就能直接生效, 交还失败不影响"点亮/熄灭"本身。
// 需要向调用者报告成功的只有最终那次 brightness 写入。
//
// 顺带一个效果: 因为开/关都会清 trigger, 所以"心跳停不下来"的问题也一并缓解了
// —— 点一下【LED 开】或【LED 关】就等于停掉心跳。
// ---------------------------------------------------------------------------
int led_onboard_on()
{
    useable_tools::write_File(Led_trigger_Path, "none");
    return useable_tools::write_File(Led_on_board_Path, "1");
}

int led_onboard_off()
{
    useable_tools::write_File(Led_trigger_Path, "none");
    return useable_tools::write_File(Led_on_board_Path, "0");
}

int buzzer_onboard_on()
{
    useable_tools::write_File(Buzzer_trigger_Path, "none");
    return useable_tools::write_File(Buzzer_on_board_Path, "1");
}

int buzzer_onboard_off()
{
    useable_tools::write_File(Buzzer_trigger_Path, "none");
    return useable_tools::write_File(Buzzer_on_board_Path, "0");
}

// ---------------------------------------------------------------------------
// 心跳
//
// 写的是 **trigger** 文件, 不是 brightness —— 见 hardware_path.h 里的说明。
// 往 brightness 写 "heartbeat" 内核会返回 EINVAL。
//
// 两个注意点:
//   1. 心跳期间 brightness 由内核接管, 手动写 brightness 不会生效
//      (想恢复手动控制必须先 clear_heartbeat)。
//   2. write_File 用 "w" 模式打开(截断)。sysfs 属性文件每次 write 都是独立
//      写入, 截断对它没影响; 但它对普通文件是覆盖写, 别拿它写日志。
// ---------------------------------------------------------------------------
int led_onboard_set_heartbeat()
{
    return useable_tools::write_File(Led_trigger_Path, "heartbeat");
}

int buzzer_onboard_set_heartbeat()
{
    return useable_tools::write_File(Buzzer_trigger_Path, "heartbeat");
}

int led_onboard_clear_heartbeat()
{
    // 写 "none" 而不是 "" —— 空字符串会被内核当作无效输入。
    return useable_tools::write_File(Led_trigger_Path, "none");
}

int buzzer_onboard_clear_heartbeat()
{
    return useable_tools::write_File(Buzzer_trigger_Path, "none");
}


// ---------------------------------------------------------------------------
// 无源蜂鸣器/开与关
// ---------------------------------------------------------------------------

// 定义 (声明在 start.h): 由 gui.cpp 的按钮回调设置, 这里读取。
bool out_buzzer_status = false;

int buzzer_set_beep()
{
    if (out_buzzer_status)
    {
        useable_tools::gpio_write_value("GPIOA", 6, "out_buzzer", 1);
        usleep(1);
        useable_tools::gpio_write_value("GPIOA", 6, "out_buzzer", 0);
        usleep(1);
    }
    return 0;
}