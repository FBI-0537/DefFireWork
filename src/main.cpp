#include "greeting.h"

#include <iostream>

/*
*   项目实际代码块（当前意义不明）
*
*   读文件里的浮点数用 useable_tools::read_File，用法:
*       #include "usetools.h"
*       float v[8];
*       int n = useable_tools::read_File("data.txt", v, 8);
*       // n < 0 表示文件打不开; 否则前 n 个元素有效
*/

int main()
{
    std::cout << firecontrol::greeting() << std::endl;
    return 0;
}
