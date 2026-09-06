#pragma once
// 轻量单元测试框架（零依赖、仅标准库、跨平台）。
//
// 用法：
//   - 在测试模块文件（test_*.cpp）中以 TEST_CASE("Suite.Name") { ... } 定义用例，
//     用例通过静态注册自动汇集，无需手工登记。
//   - 断言使用 CHECK(cond)：失败时打印 表达式 + 文件:行号 并累计失败数（不中断后续断言）。
//   - 关系/容差断言：CHECK_EQ / CHECK_NE / CHECK_LT / CHECK_LE / CHECK_GT / CHECK_GE，
//     以及浮点容差 CHECK_NEAR(lhs, rhs, eps)。比较式不流式打印值（避免依赖类型 operator<<），
//     只打印表达式与文件:行号，保证任意可比较类型都能编译。
//   - 致命断言 REQUIRE(cond)：失败时打印并立即中止当前用例（通过内建 TestAbort 异常跳出），
//     用于"前置条件无法继续"的场景。与 CHECK 语义互补：CHECK 累计所有失败，REQUIRE 短路当前用例。
//   - 入口 test_main.cpp 调用 BigHero::Test::RunAllTests() 统一运行并按结果返回退出码，
//     与 CTest/CI 的退出码约定兼容（0=全绿，非 0=有用例失败）。
//
// 运行器支持可选 CLI 过滤参数：RunAllTests(filter) 只运行名称包含 filter 的用例，
// 便于 CI 定向回归（如 ctest -R 或直接传参）。每用例输出执行耗时，便于定位慢用例。

#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
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

// 致命断言（REQUIRE）触发的中止异常：跑测器捕获它来跳过当前用例剩余断言。
// 继承自 std::exception 以便被通用的 catch(...) 也视为异常；可携带自定义原因。
struct TestAbort : std::exception
{
    const char* what() const noexcept override { return "REQUIRE failed (test case aborted)"; }
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

// REQUIRE 宏后端：失败即打印并抛出 TestAbort 中止当前用例。
inline void RequireImpl(bool cond, const char* expr, const char* file, int line)
{
    ++CheckCount();
    if (!cond)
    {
        std::printf("FAIL: REQUIRE(%s)  (%s:%d)\n", expr, file, line);
        ++FailureCount();
        throw TestAbort{};
    }
}

// 关系断言后端（比较表达式，成功打印表达式本身；不依赖值流式化）。
template<typename A, typename B>
inline void CheckRelImpl(const A& a, const B& b, bool ok, const char* sym, const char* ea, const char* eb,
                         const char* file, int line)
{
    (void)a;
    (void)b;
    ++CheckCount();
    if (!ok)
    {
        std::printf("FAIL: %s %s %s  (%s:%d)\n", ea, sym, eb, file, line);
        ++FailureCount();
    }
}

// 浮点容差断言后端：|lhs - rhs| <= eps。
inline void CheckNearImpl(double lhs, double rhs, double eps, const char* el, const char* er, const char* file,
                          int line)
{
    ++CheckCount();
    const double diff = std::fabs(lhs - rhs);
    if (diff > eps)
    {
        std::printf("FAIL: |%s - %s| <= %g  (diff=%g, lhs=%g, rhs=%g)  (%s:%d)\n", el, er, eps, diff, lhs, rhs, file,
                    line);
        ++FailureCount();
    }
}

// 运行已注册用例，仅执行名称包含 filter 的用例（filter 为空则全部执行）。
// 逐用例汇报 [ RUN ] / [ OK ] / [ FAILED ] 及耗时，最后输出汇总。
// 返回进程退出码：全部通过时返回 0，否则返回 1（CTest 约定）。
inline int RunAllTests(const char* filter = nullptr)
{
    int failedCases = 0;
    size_t runCases = 0;
    for (const TestCase& tc : Registry())
    {
        if (filter != nullptr && std::strstr(tc.name, filter) == nullptr)
            continue; // 过滤：名称不含子串则跳过
        const int failuresBefore = FailureCount();
        const auto t0 = std::chrono::steady_clock::now();
        std::printf("[ RUN      ] %s\n", tc.name);
        try
        {
            tc.fn();
        }
        catch (const TestAbort&)
        {
            // REQUIRE 触发：当前用例已被中止，剩余断言跳过。
            std::printf("  [ ABORTED ] REQUIRE failed, remaining assertions skipped.\n");
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        const int caseFailures = FailureCount() - failuresBefore;
        ++runCases;
        if (caseFailures == 0)
        {
            std::printf("[       OK ] %s (%lld ms)\n", tc.name, static_cast<long long>(ms));
        }
        else
        {
            std::printf("[  FAILED  ] %s (%d assertion failure(s), %lld ms)\n", tc.name, caseFailures,
                        static_cast<long long>(ms));
            ++failedCases;
        }
    }
    std::printf("==========================================\n");
    std::printf("%zu registered, %zu ran, %d check(s), %d failure(s)\n", Registry().size(), runCases, CheckCount(),
                FailureCount());
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

#define CHECK_EQ(a, b) ::BigHero::Test::CheckRelImpl((a), (b), (a) == (b), "==", #a, #b, __FILE__, __LINE__)
#define CHECK_NE(a, b) ::BigHero::Test::CheckRelImpl((a), (b), (a) != (b), "!=", #a, #b, __FILE__, __LINE__)
#define CHECK_LT(a, b) ::BigHero::Test::CheckRelImpl((a), (b), (a) < (b), "<", #a, #b, __FILE__, __LINE__)
#define CHECK_LE(a, b) ::BigHero::Test::CheckRelImpl((a), (b), (a) <= (b), "<=", #a, #b, __FILE__, __LINE__)
#define CHECK_GT(a, b) ::BigHero::Test::CheckRelImpl((a), (b), (a) > (b), ">", #a, #b, __FILE__, __LINE__)
#define CHECK_GE(a, b) ::BigHero::Test::CheckRelImpl((a), (b), (a) >= (b), ">=", #a, #b, __FILE__, __LINE__)

// 浮点容差断言：|lhs - rhs| <= eps。
#define CHECK_NEAR(lhs, rhs, eps)                                                                                      \
    ::BigHero::Test::CheckNearImpl(static_cast<double>(lhs), static_cast<double>(rhs), static_cast<double>(eps), #lhs, \
                                   #rhs, __FILE__, __LINE__)

// 致命断言：失败即中止当前用例。用于"前置不满足则无法继续"的场景。
#define REQUIRE(cond) ::BigHero::Test::RequireImpl(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

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
