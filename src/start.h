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

// 外部无源蜂鸣器 (接在 GPIOA:6) 开 / 关。
//
// 实现要点: **发声在专用线程里**按固定频率翻转电平, 不依赖主循环 —— 挂在主循环上
// 时音调会被循环周期锁死在几百 Hz(板上实测"音调很低")。频率默认 2 kHz, 可用环境变量
// FIRECONTROL_BUZZER_HZ 覆盖(100..8000), 方便在板上试出这个蜂鸣器最响的音。
// 返回值: 0 = 请求已接受; -1 = 发声线程起不来(具体原因打印到 stderr)。
// 注意: 打不开 GPIO 属于**线程内的异步失败**, 会打一行 stderr 并把状态退回"停",
// 不在这里返回 —— 所以控制台那行才是权威, 界面上显示"已开"只是"请求已发"。
int buzzer_out_set(bool on);

// 收尾: 停声、把 PA6 还给系统、并结束发声线程(join —— 不是丢一个 detached 野线程)。
// 界面退出前调一次; 之后再 buzzer_out_set(true) 仍可重新起来。
void buzzer_out_shutdown();

#endif
