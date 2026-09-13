#ifndef FIRECONTROL_START_H
#define FIRECONTROL_START_H

//这里放置真正的代码位置

int cpp_start();

// LED 测试用: 板载 sys-led 开 / 关。
// 返回值 0 成功, -1 失败 (开发机上没有这个设备路径, 返回 -1 属正常)。
int led_on();
int led_off();

#endif
