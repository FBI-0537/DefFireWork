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

} // namespace useable_tools
