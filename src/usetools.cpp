#include "usetools.h"

#include <cstdio>

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

} // namespace useable_tools
