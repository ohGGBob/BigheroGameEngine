// BigHero 引擎单元测试入口。
// 2026-09-04 测试工程化重构：测试用例分布于各 test_*.cpp 模块（TEST_CASE 静态注册），
// 本文件仅负责汇总运行。返回码与 CTest/CI 约定一致（0=全部通过）。
//
// 支持可选 CLI 过滤参数：./BigHeroTests [子串]，只运行名称包含子串的用例，
// 便于定向回归。无参数时运行全部。
#include "framework/test_assert.h"

int main(int argc, char** argv)
{
    const char* filter = (argc > 1) ? argv[1] : nullptr;
    return BigHero::Test::RunAllTests(filter);
}
