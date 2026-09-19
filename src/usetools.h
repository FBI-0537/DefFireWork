#ifndef FIRECONTROL_USETOOLS_H
#define FIRECONTROL_USETOOLS_H

namespace useable_tools
{
    // 从文本文件读浮点数。
    //
    // 返回值: 读到并写入 data 的元素个数(>=0); 打开文件失败返回 -1。
    //   - 数据不足 max_count 个时, 返回实际读到的个数(不报错)
    //   - 分隔符不重要: scanf 的 %f 会跳过空格/换行/制表符, 所以
    //     单值、一行多值、多行(每行一个) 都能读
    //
    // 用法:
    //     float v[8];
    //     int n = useable_tools::read_File("data.txt", v, 8);
    //     if (n < 0) { /* 文件打不开 */ }
    //     else       { /* 前 n 个元素有效 */ }
    //
    // 注意: 这是文本解析, 不是二进制读。文件里必须是可读的数字文本。
    int read_File(const char *FILE_Path, float *data, int max_count);

    // 把字符串原样写入文本文件(覆盖写, 不是追加)。
    //
    // 返回值: 成功 0; 失败(打不开文件 / 写盘出错 / 磁盘满) 返回 -1。
    //         与 read_File 一致 —— 本模块统一约定"负值即失败"。
    //
    // 用法:
    //     if (useable_tools::write_File("out.txt", "温度 100%") < 0) { /* 失败 */ }
    //
    // 注意: data 是**数据**不是格式串 —— 里面的 % 会原样写进文件。
    int write_File(const char *path, const char *data);

    // 读一条 GPIO 输入线的电平。给接在 GPIO 上的开关量输入用 —— 具体接了哪些
    // 传感器、接在哪条线上, 见 src/start.cpp 的 kGpioInputs(那张表现在是空的,
    // 要板上实测后再填)。板载 LED 与蜂鸣器走 sysfs, 不需要这个。
    //
    // 返回值: 0 或 1; 失败返回 -1 (与 read_File / write_File 的"负值即失败"一致)。
    //
    // 参数:
    //     chip_label  —— **GPIO 控制器**的 label, 例如 "gpiochip0" / "GPIOA"。
    //                    不是 "sys-led" / "beep" 这种设备名: 那些是内核 LED 框架
    //                    注册的设备, 引脚已经归驱动持有, 这里再 request 会失败
    //                    (EBUSY)。label 用 `gpiodetect` 查。
    //     line_offset —— 该控制器内的线号, 即 `gpioinfo` 里的 line 编号。
    //     consumer    —— 传给内核的消费者名字; 排查时 `gpioinfo` 的 "used by"
    //                    会显示它, 建议填程序名。
    //
    // 支持性: 需要 libgpiod (由 CMake 的 WITH_GPIOD 控制, 默认 AUTO)。
    //     没编进去时这个函数**仍然存在**, 但会打印一行提示并返回 -1 ——
    //     不静默、也不需要调用方到处写 #ifdef。
    //     用的是 libgpiod **v1** API (Debian 12 带的是 1.6); v2 已删除这些符号。
    int gpio_read_value(const char *chip_label, unsigned int line_offset,
                        const char *consumer);
    // 写一条 GPIO 输出线的电平。
    int gpio_write_value(const char *chip_label, unsigned int line_offset,
                         const char *consumer, int value);
}

#endif // FIRECONTROL_USETOOLS_H
