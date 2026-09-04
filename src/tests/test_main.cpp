// BigHero 引擎单元测试入口。
// 2026-09-04 测试工程化重构：测试用例分布于各 test_*.cpp 模块（TEST_CASE 静态注册），
// 本文件仅负责汇总运行。返回码与 CTest/CI 约定一致（0=全部通过）。
#include "framework/test_assert.h"

int main()
{
    return BigHero::Test::RunAllTests();
}
