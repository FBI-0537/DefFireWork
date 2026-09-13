#ifndef FIRECONTROL_HARDWARE_PATH_H
#define FIRECONTROL_HARDWARE_PATH_H

// ---------------------------------------------------------------------------
// 正点原子 STM32MP135 开发板上原始硬件
//
// 这些是"设备路径常量", 不是可变状态, 所以:
//   inline     —— C++17 起, 头文件里可以安全地定义变量: 所有翻译单元共享
//                同一个实体。少了它, 每个 #include 这个头文件的 .cpp 都会
//                生成一份**独立的**副本 (内部链接), 改一份不影响另一份,
//                而且编译器不会报错 —— 是很难查的那类 bug。
//   constexpr  —— 值在编译期就定下来, 直接进只读段。
//
// 命名按现有代码的风格保留下来, 没有改成 kXxx。
// ---------------------------------------------------------------------------

// LED
inline constexpr const char *Led_on_board_Path = "/sys/class/leds/sys-led/brightness";

// 蜂鸣器
inline constexpr const char *Buzzer_on_board_Path = "/sys/class/leds/beep/brightness";

// ---------------------------------------------------------------------------
// trigger 文件 —— 和 brightness 是两个独立的 sysfs 属性
//
// 内核 LED 框架里, brightness 是"亮度值", trigger 是"由谁来控制亮度":
//     echo none      > trigger    # 交还给 brightness, 手动控制
//     echo heartbeat > trigger    # 内核按心跳节奏自动闪烁
//     echo timer     > trigger    # 内核按定时器闪烁
//
// 往 brightness 里写 "heartbeat" 是错的 —— 那个文件只认数字, 内核返回 EINVAL。
// 想让它闪就得写 trigger 文件, 这也是本文件要单独列这两个路径的原因。
//
// LED 和蜂鸣器**都**支持 heartbeat (见《ATK-DLMP135 功能测试》4.1):
//     sys-led:  echo none|heartbeat > trigger, echo 1|0 > brightness
//     beep   :  echo none|heartbeat > trigger, echo 1|0 > brightness
// 注意恢复手动控制要先写 trigger=none, 否则 trigger 会一直覆盖 brightness。
// ---------------------------------------------------------------------------
inline constexpr const char *Led_trigger_Path = "/sys/class/leds/sys-led/trigger";
inline constexpr const char *Buzzer_trigger_Path = "/sys/class/leds/beep/trigger";

#endif
