#ifndef FIRECONTROL_START_H
#define FIRECONTROL_START_H

//这里放置真正的代码位置

int cpp_start();

// ---------------------------------------------------------------------------
// 板载硬件控制 (正点原子 ATK-DLMP135 主板上原生的 LED 和蜂鸣器)
//
// 这些是**动作**: 调用一次执行一次, 不是"渲染"。按钮回调里直接调用。
// 命名带 _onboard_ 是为了和将来外接的传感器/执行器区分开。
//
// 返回值: 0 成功, -1 失败。
// 开发机上这两个设备路径都不存在, 返回 -1 属正常 —— 拷到板上才是 0。
// ---------------------------------------------------------------------------

// LED: 写 "1" 常亮, "0" 熄灭。
int led_onboard_on();
int led_onboard_off();

// 蜂鸣器 (beep): 写 "1" 响, "0" 停。
int buzzer_onboard_on();
int buzzer_onboard_off();

// 心跳: 让内核以 heartbeat 节奏自动闪烁, 用来确认程序在跑。
//
// 注意: 内核 LED 的 trigger 是**独立的 sysfs 文件**(/sys/class/leds/<名字>/trigger),
// 不是 brightness。下面两个函数往 trigger 文件写 "heartbeat"。
// 路径见 hardware_path.h 的 Led_trigger_Path / Buzzer_trigger_Path。
//
// 恢复常亮/熄灭要先写 trigger="none", 再写 brightness —— 否则 trigger 会一直
// 覆盖 brightness 的值。
int led_onboard_set_heartbeat();
int buzzer_onboard_set_heartbeat();

// 关掉心跳, 交还给 brightness 控制 (trigger 写 "none")。
int led_onboard_clear_heartbeat();
int buzzer_onboard_clear_heartbeat();

// 外部无源蜂鸣器 (接在 GPIOA:6) 的开关状态: true = 要响, false = 停。
// GUI 的按钮回调设置它 (gui.cpp), buzzer_out_tick() 读取它 (start.cpp),
// 所以必须是全局的、两个文件都看得见 —— 早先写成 main() 里的局部变量,
// 结果两边都报 not declared。
extern bool out_buzzer_status;

// 推进外部无源蜂鸣器的方波。**主循环每轮调一次**(不是"响一下") —— 无源蜂鸣器
// 要的是持续方波, 不是单个脉冲。内部按时间翻转 GPIOA:6 的电平, 是否发声由
// out_buzzer_status 决定; 关着时什么都不做。详见 start.cpp 里的说明。
int buzzer_out_tick();

#endif
