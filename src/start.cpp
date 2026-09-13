#include "start.h"

#include <cstdio>

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
// 状态采集: 读一次当前状态, 供 gui_build() 和 render() 使用。
// 将来的周期采集也应该走这里, 保证"状态怎么来的"只有一处。
// ---------------------------------------------------------------------------
int status()
{
    // TODO: 读传感器, 算出当前火警等级。

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
//需要注意的是，这些硬件都是主板原有的硬件，所以加上_onboard_，以区分。
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
