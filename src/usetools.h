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
}

#endif // FIRECONTROL_USETOOLS_H
