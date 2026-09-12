#ifndef FIRECONTROL_GREETING_H
#define FIRECONTROL_GREETING_H

#include <string>

namespace firecontrol {

// 返回问候语。单独抽成函数是为了能被单元测试覆盖 ——
// 逻辑和 main() 分开, 是让工程能长大、能测试的第一步。
std::string greeting();

} // namespace firecontrol

#endif // FIRECONTROL_GREETING_H
