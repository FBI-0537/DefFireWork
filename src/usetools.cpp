#include "usetools.h"

#include <cstdio>

// libgpiod 是可选的 (CMake 的 WITH_GPIOD, 默认 AUTO): 开发机上没装
// libgpiod-devel 时整个项目仍然要能编。所以头文件和实现都跟着这个宏走 ——
// 宏由 firecontrol 目标 PUBLIC 导出, 声明与实现不会各编一边。
#if defined(FC_HAVE_GPIOD)
#include <gpiod.h>
#endif

namespace useable_tools
{

int read_File(const char *FILE_Path, float *data, int max_count)
{
    if (FILE_Path == nullptr || data == nullptr || max_count <= 0) {
        return -1;
    }

    FILE *fp = fopen(FILE_Path, "r");
    if (fp == nullptr) {
        perror(FILE_Path);
        return -1;
    }

    // 循环读, 直到读满、或遇到非数字内容(文件结束/格式不符)。
    // %f 会跳过空白字符, 所以一行多个、一行一个、空格/制表符分隔都能处理。
    int n = 0;
    while (n < max_count && fscanf(fp, "%f", &data[n]) == 1) {
        n++;
    }

    fclose(fp);
    return n;
}

// 覆盖写入文本。data 按原样写出, 不做任何格式化解释。
// 返回 0 成功 / -1 失败, 与 read_File 的"负值即失败"约定保持一致。
int write_File(const char *path, const char *data)
{
    if (path == nullptr || data == nullptr) {
        return -1;
    }

    FILE *fp = fopen(path, "w");
    if (fp == nullptr) {
        perror(path);
        return -1;
    }

    // fputs 而不是 fprintf: data 是数据, 不是格式串。
    // 写成 fprintf(fp, data) 的话, 文本里出现 % 会被当成格式说明符, 去取一个
    // 并不存在的参数 —— 未定义行为, 而 -Wall -Wextra 默认不会警告。
    const bool written = (fputs(data, fp) != EOF);

    // fclose 会自动把缓冲刷入文件, 所以不需要再显式 fflush;
    // 但它的返回值必须查 —— 磁盘满/写入错误往往只在刷盘这一刻才暴露。
    const bool closed = (fclose(fp) == 0);

    return (written && closed) ? 0 : -1;
}

#if defined(FC_HAVE_GPIOD)

int gpio_read_value(const char *chip_label, unsigned int line_offset, const char *consumer) {
    struct gpiod_chip *chip = nullptr;
    struct gpiod_line *line = nullptr;
    int value = -1;

    if (chip_label == nullptr || consumer == nullptr) {
        return -1;
    }

    chip = gpiod_chip_open_by_label(chip_label);
    if (!chip) {
        perror("gpiod_chip_open_by_label");
        return -1;
    }

    line = gpiod_chip_get_line(chip, line_offset);
    if (!line) {
        perror("gpiod_chip_get_line");
        gpiod_chip_close(chip);
        return -1;
    }

    if (gpiod_line_request_input(line, consumer) < 0) {
        perror("gpiod_line_request_input");
        gpiod_chip_close(chip);   // 未成功 request，不要 release
        return -1;
    }

    value = gpiod_line_get_value(line);
    if (value < 0) {
        perror("gpiod_line_get_value");
    }

    gpiod_line_release(line);
    gpiod_chip_close(chip);

    return value;
}

#else  // 没编入 libgpiod: 保留同名函数, 但显式失败 —— 不静默返回"低电平"

int gpio_read_value(const char *chip_label, unsigned int line_offset, const char *consumer) {
    (void)chip_label;
    (void)line_offset;
    (void)consumer;

    // 只提示一次: 传感器是轮询读的, 每次都打印会把 stderr 刷爆
    static bool warned = false;
    if (!warned) {
        warned = true;
        std::fprintf(stderr,
                     "gpio_read_value: 本产物没有编入 libgpiod 支持 (CMake 的 WITH_GPIOD) "
                     "—— 返回 -1。装上 libgpiod-devel 后重新配置即可。\n");
    }
    return -1;
}

#endif



} // namespace useable_tools
