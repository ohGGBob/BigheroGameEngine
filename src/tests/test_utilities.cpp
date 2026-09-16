// 工具类运行时行为测试（test_utilities）。
//
// 背景：src/core 中大量“纯逻辑”工具类此前只有“自包含编译检查”，缺乏运行时行为回归。
// 本模块补齐 Easing/EasingCurve/CurveKey/StringUtils/PathUtils/Uuid/ScopeGuard/
// Stopwatch/HashUtils/StringId/Time 的功能与边界测试。
#include "framework/test_common.h"

#include "core/CurveKey.h"
#include "core/Easing.h"
#include "core/EasingCurve_v2.h"
#include "core/HashUtils.h"
#include "core/PathUtils.h"
#include "core/ScopeGuard.h"
#include "core/Stopwatch.h"
#include "core/StringId.h"
#include "core/StringUtils.h"
#include "core/Time.h"
#include "core/Uuid.h"

#include <thread>

TEST_CASE("Utilities.Easing")
{
    namespace E = BigHero::Core::Easing;
    constexpr float kEps = 1e-4f;

    // 端点一致性：所有缓动在 t=1 都收敛到 1。
    CHECK_NEAR(E::Linear(1.0f), 1.0f, kEps);
    CHECK_NEAR(E::InQuad(0.0f), 0.0f, kEps);
    CHECK_NEAR(E::InQuad(1.0f), 1.0f, kEps);
    CHECK_NEAR(E::OutQuad(0.5f), 0.75f, kEps); // t*(2-t)=0.5*1.5
    CHECK_NEAR(E::InOutQuad(0.5f), 0.5f, kEps);
    CHECK_NEAR(E::InCubic(0.5f), 0.125f, kEps);
    CHECK_NEAR(E::OutCubic(0.5f), 0.875f, kEps);
    CHECK_NEAR(E::InOutCubic(0.5f), 0.5f, kEps);

    // Back/Elastic 端点仍应精确落位。
    CHECK_NEAR(E::OutBack(0.0f), 0.0f, kEps);
    CHECK_NEAR(E::OutBack(1.0f), 1.0f, kEps);
    CHECK_NEAR(E::OutElastic(0.0f), 0.0f, kEps);
    CHECK_NEAR(E::OutElastic(1.0f), 1.0f, kEps);
    CHECK_NEAR(E::SmoothStep(0.5f), 0.5f, kEps);
    CHECK_NEAR(E::SmoothStep(0.0f), 0.0f, kEps);
    CHECK_NEAR(E::SmoothStep(1.0f), 1.0f, kEps);

    // 单调性（线性段内）：缓动应严格递增。
    CHECK(E::InQuad(0.4f) < E::InQuad(0.6f));
    CHECK(E::OutCubic(0.4f) < E::OutCubic(0.6f));
}

TEST_CASE("Utilities.EasingCurve")
{
    bighero::EasingCurve c;
    CHECK(c.KeyCount() == 0);
    CHECK_NEAR(c.Sample(0.5f), 0.0f, 1e-5f); // 空曲线取 0

    // 乱序插入应按 t 排序。
    c.AddKey(1.0f, 10.0f);
    c.AddKey(0.0f, 0.0f);
    CHECK(c.KeyCount() == 2);
    CHECK_NEAR(c.KeyAt(0).t, 0.0f, 1e-5f);
    CHECK_NEAR(c.KeyAt(1).t, 1.0f, 1e-5f);
    CHECK_NEAR(c.StartValue(), 0.0f, 1e-5f);
    CHECK_NEAR(c.EndValue(), 10.0f, 1e-5f);

    // 线性采样：t=0.5 → 5；区间外钳制到端点。
    CHECK_NEAR(c.Sample(0.5f), 5.0f, 1e-4f);
    CHECK_NEAR(c.Sample(-1.0f), 0.0f, 1e-5f);
    CHECK_NEAR(c.Sample(2.0f), 10.0f, 1e-5f);

    // smoothstep 中点同值但曲线更陡（0.25 处 < 线性）。
    CHECK_NEAR(c.SampleSmooth(0.5f), 5.0f, 1e-4f);
    CHECK(c.SampleSmooth(0.25f) < c.Sample(0.25f));

    c.Clear();
    CHECK(c.KeyCount() == 0);
}

TEST_CASE("Utilities.CurveKey")
{
    bighero::CurveKey linear(0.0f, 0.0f, bighero::CurveKey::Mode::Linear);
    CHECK_NEAR(linear.Evaluate(10.0f, 0.5f), 5.0f, 1e-5f);
    CHECK_NEAR(linear.Evaluate(10.0f, 0.0f), 0.0f, 1e-5f);
    CHECK_NEAR(linear.Evaluate(10.0f, 1.0f), 10.0f, 1e-5f);

    bighero::CurveKey constant(0.0f, 3.0f, bighero::CurveKey::Mode::Constant);
    CHECK_NEAR(constant.Evaluate(10.0f, 0.9f), 3.0f, 1e-5f); // 保持值

    bighero::CurveKey smooth(0.0f, 0.0f, bighero::CurveKey::Mode::Smooth);
    CHECK_NEAR(smooth.Evaluate(10.0f, 0.5f), 5.0f, 1e-4f);

    bighero::CurveKey bez(0.0f, 0.0f, bighero::CurveKey::Mode::Bezier);
    bez.SetTangents(0.0f, 0.0f);
    CHECK_NEAR(bez.Evaluate(10.0f, 0.5f), 5.0f, 1e-4f);

    // localT 越界应被钳制。
    CHECK_NEAR(linear.Evaluate(10.0f, 2.0f), 10.0f, 1e-5f);
    CHECK_NEAR(linear.Evaluate(10.0f, -1.0f), 0.0f, 1e-5f);
}

TEST_CASE("Utilities.StringUtils")
{
    namespace S = BigHero::Core::str;
    CHECK(S::Trim("  hi \t\n") == "hi");
    CHECK(S::Trim("nospace") == "nospace");
    CHECK(S::ToLower("HeLLo") == "hello");
    CHECK(S::ToUpper("HeLLo") == "HELLO");
    CHECK(S::StartsWith("engine.exe", "engine"));
    CHECK(!S::StartsWith("engine", "engine.exe"));
    CHECK(S::EndsWith("texture.png", ".png"));
    CHECK(!S::EndsWith("png", ".png"));

    auto parts = S::Split("a,b,,c", ',');
    CHECK(parts.size() == 4);
    CHECK(parts[0] == "a" && parts[1] == "b" && parts[2].empty() && parts[3] == "c");
    auto compact = S::Split("a,b,,c", ',', true);
    CHECK(compact.size() == 3);
    CHECK(compact[0] == "a" && compact[2] == "c");

    std::vector<std::string> v = {"x", "y", "z"};
    CHECK(S::Join(v.begin(), v.end(), "-") == "x-y-z");
    CHECK(S::Replace("a-b-c", "-", "+") == "a+b+c");
    CHECK(S::Replace("abc", "", "+") == "abc"); // 空 from 原样返回

    int iv = 0;
    CHECK(S::ParseInt("42", iv) && iv == 42);
    CHECK(S::ParseInt("-7", iv) && iv == -7);
    CHECK(!S::ParseInt("42x", iv));
    CHECK(!S::ParseInt("", iv));
    double dv = 0;
    CHECK(S::ParseFloat("3.14", dv));
    CHECK_NEAR(dv, 3.14, 1e-9);
    CHECK(!S::ParseFloat("abc", dv));
    CHECK(!S::ParseFloat("", dv));

    CHECK(S::ToString(3.0f) == "3"); // 整值浮点走短路径
    CHECK(S::ToString(123) == "123");
    CHECK(S::Format("raw") == "raw");
}

TEST_CASE("Utilities.PathUtils")
{
    using namespace BigHero::Core;
    CHECK(NormalizePath("a/./b") == "a/b");
    CHECK(NormalizePath("a//b") == "a/b");
    CHECK(NormalizePath("a/../b") == "b");
    CHECK(NormalizePath("C:\\Users\\a\\..\\b") == "C:/Users/b");
    CHECK(NormalizePath("") == "");
    CHECK(NormalizePath(".") == ".");
    CHECK(NormalizePath("/../x") == "/x"); // 绝对路径不回退

    CHECK(JoinPath("dir", "file.txt") == "dir/file.txt");
    CHECK(JoinPath("dir/", "file.txt") == "dir/file.txt");
    CHECK(JoinPath("", "file.txt") == "file.txt");

    CHECK(GetFileName("a/b/file.txt") == "file.txt");
    CHECK(GetBaseName("a/b/file.txt") == "file");
    CHECK(GetExtension("a/b/file.txt") == ".txt");
    CHECK(GetExtension("a/b/file.txt", false) == "txt");
    CHECK(GetExtension("noext") == "");
    CHECK(GetParentDir("a/b/c") == "a/b");
    CHECK(GetParentDir("c") == ".");

    CHECK(IsAbsolutePath("/usr/lib"));
    CHECK(IsAbsolutePath("C:/Windows"));
    CHECK(!IsAbsolutePath("rel/path"));
    CHECK(IsRelativePath("rel/path"));
    CHECK(ChangeExtension("a.txt", ".md") == "a.md");
    CHECK(ChangeExtension("noext", ".md") == "noext.md");
}

TEST_CASE("Utilities.Uuid")
{
    using BigHero::Core::Uuid;
    Uuid nil = Uuid::Nil();
    CHECK(nil.IsNil());

    Uuid a = Uuid::Generate();
    Uuid b = Uuid::Generate();
    CHECK(!a.IsNil());
    CHECK(a != b); // 两次随机生成几乎不可能相同

    // 标准 8-4-4-4-12 格式。
    std::string s = a.ToString();
    CHECK(s.size() == 36);
    CHECK(s[8] == '-' && s[13] == '-' && s[18] == '-' && s[23] == '-');

    // 版本/变体位。
    Uuid round = Uuid::FromString(s);
    CHECK(round == a);                      // round-trip
    CHECK((round.bytes[6] & 0xF0) == 0x40); // version 4
    CHECK((round.bytes[8] & 0xC0) == 0x80); // variant 10xx

    CHECK(Uuid::FromString("not-a-uuid").IsNil()); // 非法 → nil
    CHECK(Uuid::FromString("").IsNil());
    // 无连字符形式也接受。
    std::string noDash;
    for (char c : s)
        if (c != '-')
            noDash += c;
    CHECK(Uuid::FromString(noDash) == a);
}

TEST_CASE("Utilities.ScopeGuard")
{
    using namespace BigHero::Core;
    int exited = 0;
    {
        auto g = MakeScopeGuard([&] { ++exited; });
        CHECK(g.Active());
        CHECK(exited == 0);
    }
    CHECK(exited == 1);

    // Dismiss 取消执行。
    {
        auto g = MakeScopeExit([&] { ++exited; });
        g.Dismiss();
        CHECK(!g.Active());
    }
    CHECK(exited == 1);

    // 异常路径也必须执行。
    bool ranOnThrow = false;
    try
    {
        auto g = MakeScopeGuard([&] { ranOnThrow = true; });
        throw std::runtime_error("x");
    }
    catch (const std::runtime_error&)
    {
    }
    CHECK(ranOnThrow);
}

TEST_CASE("Utilities.Stopwatch")
{
    using BigHero::Core::Stopwatch;
    Stopwatch sw;
    CHECK(!sw.IsRunning());
    CHECK_NEAR(sw.ElapsedSeconds(), 0.0, 1e-9);

    sw.Start();
    CHECK(sw.IsRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    sw.Stop();
    CHECK(!sw.IsRunning());
    CHECK(sw.ElapsedSeconds() > 0.0);
    CHECK(sw.ElapsedMilliseconds() >= 1.0);
    const double first = sw.ElapsedSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK_NEAR(sw.ElapsedSeconds(), first, 1e-9); // Stop 后冻结

    sw.Reset();
    CHECK(!sw.IsRunning());
    CHECK_NEAR(sw.ElapsedSeconds(), 0.0, 1e-9);
    CHECK(sw.Lap() >= 0.0); // 单次采样
}

TEST_CASE("Utilities.Hash")
{
    namespace H = BigHero::Core::hash;
    // FNV-1a：默认种子即 offset basis，空串返回种子本身。
    CHECK(H::Fnv1a32(std::string_view("")) == 2166136261u);
    CHECK(H::Fnv1a64(std::string_view("")) == 1469598103934665603ULL);

    // 稳定性与区分度。
    CHECK(H::Fnv1a32(std::string_view("player")) == H::Fnv1a32(std::string_view("player")));
    CHECK(H::Fnv1a32(std::string_view("player")) != H::Fnv1a32(std::string_view("enemy")));
    CHECK(H::Fnv1a64(std::string_view("player")) != H::Fnv1a64(std::string_view("enemy")));

    // 种子影响结果。
    CHECK(H::Fnv1a32(std::string_view("x"), 1u) != H::Fnv1a32(std::string_view("x"), 2u));

    // Murmur3：空串（len=0）在 seed=0 下回到 0。
    CHECK(H::Murmur3(std::string_view("")) == 0u);
    CHECK(H::Murmur3(std::string_view("abc")) == H::Murmur3(std::string_view("abc")));
    CHECK(H::Murmur3(std::string_view("abc")) != H::Murmur3(std::string_view("abd")));
}

TEST_CASE("Utilities.StringId")
{
    using BigHero::Core::StringId;
    CHECK(StringId::Intern("") == StringId::kInvalid); // 空串非法

    const auto idA = StringId::Intern("MeshComponent");
    const auto idB = StringId::Intern("MeshComponent");
    const auto idC = StringId::Intern("CameraComponent");
    CHECK(idA != StringId::kInvalid);
    CHECK(idA == idB); // 同名同 ID
    CHECK(idA != idC); // 异名异 ID
    CHECK(StringId::IsInterned("MeshComponent"));
    CHECK(!StringId::IsInterned("NeverRegistered"));
    CHECK(StringId::Resolve(idA) == std::string_view("MeshComponent"));
    CHECK(StringId::Resolve(0u).empty()); // 未注册 ID → 空
}

TEST_CASE("Utilities.Time")
{
    const double t0 = BigHero::Time::NowSeconds();
    CHECK(t0 >= 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const double t1 = BigHero::Time::NowSeconds();
    CHECK(t1 >= t0); // 单调不减
    CHECK(t1 - t0 >= 0.0);
}
