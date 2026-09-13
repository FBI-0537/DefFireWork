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

#endif
