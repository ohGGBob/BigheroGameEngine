#pragma once
// 轻量单元测试框架（零依赖、仅标准库、跨平台）。
//
// 用法：
//   - 在测试模块文件（test_*.cpp）中以 TEST_CASE("Suite.Name") { ... } 定义用例，
//     用例通过静态注册自动汇集，无需手工登记。
//   - 断言使用 CHECK(cond)：失败时打印 表达式 + 文件:行号 并累计失败数（不中断后续断言）。
//   - 入口 test_main.cpp 调用 BigHero::Test::RunAllTests() 统一运行并按结果返回退出码，
//     与 CTest/CI 的退出码约定兼容（0=全绿，非 0=有用例失败）。

#include <cstddef>
#include <cstdio>
#include <vector>

namespace BigHero::Test
{
// 用例描述：名称 + 函数指针 + 注册来源文件。
struct TestCase
{
    const char* name;
    void (*fn)();
    const char* file;
};

// 全局用例注册表（函数内静态，规避跨翻译单元静态初始化顺序问题）。
inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

inline int& CheckCount()
{
    static int n = 0;
    return n;
}

inline int& FailureCount()
{
    static int n = 0;
    return n;
}

// TEST_CASE 静态注册器：构造时把用例登记进注册表。
struct AutoRegister
{
    AutoRegister(const char* name, void (*fn)(), const char* file) { Registry().push_back(TestCase{name, fn, file}); }
};

// CHECK 宏的唯一后端。
inline void CheckImpl(bool cond, const char* expr, const char* file, int line)
{
    ++CheckCount();
    if (!cond)
    {
        std::printf("FAIL: %s  (%s:%d)\n", expr, file, line);
        ++FailureCount();
    }
}

// 运行全部已注册用例，逐用例汇报 [ RUN ] / [ OK ] / [ FAILED ]，最后输出汇总。
// 返回进程退出码：全部通过返回 0，否则返回失败用例数。
inline int RunAllTests()
{
    int failedCases = 0;
    for (const TestCase& tc : Registry())
    {
        const int failuresBefore = FailureCount();
        std::printf("[ RUN      ] %s\n", tc.name);
        tc.fn();
        const int caseFailures = FailureCount() - failuresBefore;
        if (caseFailures == 0)
        {
            std::printf("[       OK ] %s\n", tc.name);
        }
        else
        {
            std::printf("[  FAILED  ] %s (%d assertion failure(s))\n", tc.name, caseFailures);
            ++failedCases;
        }
    }
    std::printf("==========================================\n");
    std::printf("%zu test case(s), %d check(s), %d failure(s)\n", Registry().size(), CheckCount(), FailureCount());
    if (failedCases == 0)
    {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test case(s) failed.\n", failedCases);
    return 1;
}
} // namespace BigHero::Test

#define CHECK(cond) ::BigHero::Test::CheckImpl(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

#define BH_TEST_CAT_INNER(a, b) a##b
#define BH_TEST_CAT(a, b) BH_TEST_CAT_INNER(a, b)

// 定义并注册一个测试用例。同一翻译单元内按行号生成唯一符号，可在多文件中任意分布。
#define TEST_CASE(name)                                                                                                \
    static void BH_TEST_CAT(TestFn_, __LINE__)();                                                                      \
    namespace                                                                                                          \
    {                                                                                                                  \
    const ::BigHero::Test::AutoRegister BH_TEST_CAT(kAutoReg_, __LINE__)(name, &BH_TEST_CAT(TestFn_, __LINE__),        \
                                                                         __FILE__);                                    \
    }                                                                                                                  \
    static void BH_TEST_CAT(TestFn_, __LINE__)()
