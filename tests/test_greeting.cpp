// test_greeting.cpp — 单元测试入口
//
// 原生构建时由 ctest 调用; 交叉编译时自动跳过 (ARM 产物跑不了)。
// 刻意不引入第三方测试框架, 保持零依赖。要更强的话可以换 Catch2 / GoogleTest。

#include "greeting.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool cond, const std::string &what)
{
    if (cond) {
        std::cout << "  [ OK ] " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        g_failures++;
    }
}

} // namespace

int main()
{
    std::cout << "FireControlApp 单元测试:\n";

    const std::string g = firecontrol::greeting();

    check(!g.empty(),                     "greeting() 非空");
    check(g == "hallo world",             "greeting() 内容正确");
    check(g.find("world") != std::string::npos, "greeting() 含 \"world\"");

    if (g_failures != 0) {
        std::cout << "[FAIL] 共 " << g_failures << " 项失败\n";
        return 1;
    }

    std::cout << "[ OK ] 全部通过\n";
    return 0;
}
