// 容器与工具类运行时行为测试（test_containers）。
//
// 背景：此前 src/core 中大量“纯逻辑”容器（SmallVector/SparseSet/FixedArray/BitFlags/
// Result/Variant/Any/PropertyBag/StringBuilder）只有“自包含编译检查”（BigHeroHeaderCheck），
// 缺乏运行时行为回归。本模块补齐这些类的功能与边界测试，锁定正确行为。
//
// 其中 SmallVector 用例包含一条回归断言：修复“迁移到堆后 pop_back 回内嵌容量时
// ptr() 误读被移动搬空的内嵌缓冲”导致的悬垂/脏读。用 std::string 作为元素类型，
// 因为其“移动后源对象被清空”的特性使该缺陷必然可复现。
#include "framework/test_common.h"

#include "core/Any.h"
#include "core/BitFlags.h"
#include "core/FixedArray.h"
#include "core/PropertyBag.h"
#include "core/Result.h"
#include "core/SmallVector.h"
#include "core/SparseSet.h"
#include "core/StringBuilder.h"
#include "core/Variant.h"

#include <string>
#include <vector>

using namespace BigHero::Core;

TEST_CASE("Containers.SmallVector")
{
    // 内嵌阶段：容量 N=3，未溢出时不应触发堆迁移。
    SmallVector<std::string, 3> v;
    CHECK(v.Empty());
    CHECK(!v.OnHeap());
    v.push_back(std::string("a"));
    v.push_back(std::string("b"));
    v.push_back(std::string("c"));
    CHECK(v.Size() == 3);
    CHECK(!v.OnHeap());
    CHECK(v[0] == "a" && v[1] == "b" && v[2] == "c");
    CHECK(v.Front() == "a" && v.Back() == "c");

    // 溢出：迁移到堆，原有元素必须完整保留（std::string 迁移后内嵌缓冲被搬空）。
    v.push_back(std::string("d"));
    CHECK(v.Size() == 4);
    CHECK(v.OnHeap());
    CHECK(v[0] == "a" && v[1] == "b" && v[2] == "c" && v[3] == "d");

    // 回归：从堆回退到 size==N 时，读取仍须命中堆数据，而非被搬空的内嵌缓冲。
    v.pop_back();
    CHECK(v.Size() == 3);
    CHECK(v[0] == "a" && v[1] == "b" && v[2] == "c");

    // 迭代与 range-for 在两种阶段都稳定。
    std::string joined;
    for (const auto& s : v)
        joined += s;
    CHECK(joined == "abc");

    // Clear 后状态干净，可复用。
    v.Clear();
    CHECK(v.Empty());
    CHECK(!v.OnHeap());
    v.push_back(std::string("x"));
    CHECK(v.Size() == 1 && v[0] == "x");

    // 越界访问抛异常。
    bool threw = false;
    try
    {
        v.At(5);
    }
    catch (const std::out_of_range&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("Containers.SparseSet")
{
    SparseSet<int> set;
    CHECK(set.Empty() && set.Size() == 0);
    CHECK(set.Insert(10));
    CHECK(set.Insert(20));
    CHECK(set.Insert(30));
    CHECK(!set.Insert(20)); // 重复插入返回 false
    CHECK(set.Size() == 3);
    CHECK(set.Contains(10) && set.Contains(30));
    CHECK(!set.Contains(99));
    CHECK(set.IndexOf(20) == 1);
    CHECK(set.IndexOf(99) == -1);

    // swap-erase：删除中间元素后仍保持紧凑（无空洞）。
    CHECK(set.Erase(20));
    CHECK(!set.Erase(20)); // 再次删除返回 false
    CHECK(set.Size() == 2);
    CHECK(!set.Contains(20));
    CHECK(set.Contains(30));

    // 遍历只访问有效元素。
    int sum = 0;
    for (int k : set)
        sum += k;
    CHECK(sum == 40); // 10 + 30

    CHECK(set.Dense().size() == 2);
    set.Clear();
    CHECK(set.Empty());
}

TEST_CASE("Containers.FixedArray")
{
    FixedArray<int, 3> a;
    CHECK(a.Empty());
    CHECK(a.Capacity() == 3);
    CHECK(!a.Full());
    a.push_back(1);
    a.push_back(2);
    a.push_back(3);
    CHECK(a.Size() == 3);
    CHECK(a.Full());
    CHECK(a[0] == 1 && a.Back() == 3);
    CHECK(a.At(1) == 2);

    // 溢出抛异常。
    bool threw = false;
    try
    {
        a.push_back(4);
    }
    catch (const std::overflow_error&)
    {
        threw = true;
    }
    CHECK(threw);

    a.pop_back();
    CHECK(a.Size() == 2 && a.Back() == 2);

    // 越界与空 pop 抛异常。
    threw = false;
    try
    {
        a.At(9);
    }
    catch (const std::out_of_range&)
    {
        threw = true;
    }
    CHECK(threw);
    a.Clear();
    threw = false;
    try
    {
        a.pop_back();
    }
    catch (const std::underflow_error&)
    {
        threw = true;
    }
    CHECK(threw);

    // 迭代。
    a.push_back(7);
    int sum = 0;
    for (int x : a)
        sum += x;
    CHECK(sum == 7);
}

TEST_CASE("Containers.BitFlags")
{
    using Flags = BitFlags<uint32_t>;
    Flags f;
    CHECK(f.None());
    CHECK(!f.Any());
    CHECK(f.Count() == 0);

    f.Set(0x1u);
    f.Set(0x4u);
    CHECK(f.Test(0x1u));
    CHECK(f.Test(0x4u));
    CHECK(!f.Test(0x2u));
    CHECK(f.Count() == 2);
    CHECK(f.HasAny(0x6u));        // 与 0x4 有交集
    CHECK(!f.HasAny(0x2u));       // 与 0x2 无交集
    CHECK(f.HasAll(0x1u | 0x4u)); // 同时含有 0x1 与 0x4
    CHECK(!f.HasAll(0x3u));       // 缺少 0x2 位

    f.Toggle(0x1u);
    CHECK(!f.Test(0x1u));
    CHECK(f.Count() == 1);

    f.Unset(0x4u);
    CHECK(f.None());

    // 位运算组合与比较。
    Flags g(0x2u);
    Flags h = g | 0x8u;
    CHECK(h.Test(0x2u) && h.Test(0x8u));
    CHECK(h.HasAll(0xA));
    h &= 0x2u;
    CHECK(h == g);
    CHECK(h != Flags(0x1u));

    f |= 0x10u;
    CHECK(f.Test(0x10u));
    f.Clear();
    CHECK(f.None());
    CHECK(f.Get() == 0u);
    f.SetValue(0x3u);
    CHECK(f.Count() == 2);
}

TEST_CASE("Containers.Result")
{
    using R = Result<int, std::string>;
    R ok = R::Ok(42);
    CHECK(ok.IsOk());
    CHECK(!ok.IsErr());
    CHECK(static_cast<bool>(ok));
    CHECK(ok.Value() == 42);
    CHECK(ok.TryValue() != nullptr && *ok.TryValue() == 42);
    CHECK(ok.ValueOr(7) == 42);
    CHECK(ok.ErrorOr(std::string("none")) == "none");

    R err = R::Err(std::string("boom"));
    CHECK(err.IsErr());
    CHECK(!static_cast<bool>(err));
    CHECK(err.Error() == "boom");
    CHECK(err.TryValue() == nullptr);
    CHECK(err.ValueOr(7) == 7);
    CHECK(err.ErrorOr(std::string("none")) == "boom");

    // 非法访问抛异常。
    bool threw = false;
    try
    {
        (void)err.Value();
    }
    catch (const std::logic_error&)
    {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try
    {
        (void)ok.Error();
    }
    catch (const std::logic_error&)
    {
        threw = true;
    }
    CHECK(threw);

    // 移动语义。
    R moved = R::Err(std::string("moved"));
    R target = std::move(moved);
    CHECK(target.IsErr() && target.Error() == "moved");
}

TEST_CASE("Containers.Variant")
{
    Variant v;
    CHECK(v.Empty());
    CHECK(!v.Has<int>());

    v.Emplace<int>(5);
    CHECK(!v.Empty());
    CHECK(v.Has<int>());
    CHECK(v.Get<int>() == 5);

    // 类型不匹配：Get 抛 bad_cast，TryGet 返回 false，GetOr 返回默认。
    bool threw = false;
    try
    {
        (void)v.Get<std::string>();
    }
    catch (const std::bad_cast&)
    {
        threw = true;
    }
    CHECK(threw);
    std::string out = "def";
    CHECK(!v.TryGet<std::string>(out));
    CHECK(v.GetOr<std::string>("fallback") == "fallback");

    // 变更类型：旧值被重置。
    v.Emplace<std::string>("hello");
    CHECK(v.Has<std::string>());
    CHECK(!v.Has<int>());
    CHECK(v.Get<std::string>() == "hello");

    // 深拷贝：拷贝后互相独立。
    Variant copy = v;
    copy.Get<std::string>() = "changed";
    CHECK(v.Get<std::string>() == "hello");
    CHECK(copy.Get<std::string>() == "changed");

    // 移动后源被重置。
    Variant m = std::move(copy);
    CHECK(m.Get<std::string>() == "changed");
    CHECK(copy.Empty());

    v.Reset();
    CHECK(v.Empty());
}

TEST_CASE("Containers.Any")
{
    Any a;
    CHECK(a.Empty());
    CHECK(!a.Has<int>());
    a.Set(11);
    CHECK(a.Has<int>());
    CHECK(a.Get<int>() == 11);

    std::string s = "default";
    CHECK(!a.TryGet<std::string>(s));

    // 类型擦除持有字符串。
    a.Set(std::string("abc"));
    CHECK(a.Has<std::string>());
    CHECK(a.Get<std::string>() == "abc");
    CHECK(a.TryGet<std::string>(s) && s == "abc");

    // 值语义深拷贝。
    Any b = a;
    b.Get<std::string>() = "xyz";
    CHECK(a.Get<std::string>() == "abc");
    CHECK(b.Get<std::string>() == "xyz");

    // 错误类型读取抛 bad_cast。
    bool threw = false;
    try
    {
        (void)a.Get<int>();
    }
    catch (const std::bad_cast&)
    {
        threw = true;
    }
    CHECK(threw);

    a.Reset();
    CHECK(a.Empty());
}

TEST_CASE("Containers.PropertyBag")
{
    PropertyBag bag;
    CHECK(bag.Size() == 0);
    bag.Set("health", 100);
    bag.Set("name", std::string("hero"));
    bag.Set("speed", 3.5f);
    CHECK(bag.Size() == 3);
    CHECK(bag.Has("health"));
    CHECK(!bag.Has("mana"));

    CHECK(bag.Get<int>("health") == 100);
    CHECK(bag.Get<std::string>("name") == "hero");
    CHECK_NEAR(bag.Get<float>("speed"), 3.5f, 1e-6f);

    // 缺失键：GetOr 返回默认；Get 抛异常。
    CHECK(bag.GetOr<int>("mana", 0) == 0);
    bool threw = false;
    try
    {
        (void)bag.Get<int>("mana");
    }
    catch (const std::out_of_range&)
    {
        threw = true;
    }
    CHECK(threw);

    // 类型不匹配：GetOr 回落到默认。
    CHECK(bag.GetOr<std::string>("health", std::string("n/a")) == "n/a");

    CHECK(bag.Erase("name"));
    CHECK(!bag.Erase("name"));
    CHECK(bag.Size() == 2);

    bag.Clear();
    CHECK(bag.Size() == 0);
}

TEST_CASE("Containers.StringBuilder")
{
    StringBuilder sb;
    CHECK(sb.Empty());
    sb.Append("a").Append('b').Append(12).Append(true);
    CHECK(sb.Str() == "ab12true");
    CHECK(sb.Size() == sb.Length());

    sb.Clear();
    CHECK(sb.Empty());

    StringBuilder lines;
    lines.AppendLine("first");
    lines.AppendLine("second");
    CHECK(lines.Str() == "first\nsecond\n");

    StringBuilder rep;
    rep.AppendRepeated("ab", 3);
    CHECK(rep.Str() == "ababab");

    StringBuilder kv;
    kv.Append("key", '=', std::string_view("value"));
    CHECK(kv.Str() == "key=value");

    // 数值类型全部可拼接。
    StringBuilder num;
    num.Append(-7).Append(' ').Append(2.5f).Append(' ').Append(123456789012345LL);
    CHECK(num.Str() == "-7 2.5 123456789012345");

    sb.Reserve(64);
    CHECK(sb.Empty());
    sb.MutBuf() += "z";
    CHECK(sb.ToString() == "z");
}
