// 内存分配器与数学工具运行时行为测试（test_memory_math）。
//
// 背景：MemoryArena / PoolAllocator / ObjectPool / RingBuffer / BitVector 等
// “纯逻辑”基础设施此前仅有自包含编译检查，缺乏运行时行为回归；Vector3 / MathUtils
// 同为引擎基础数学，本模块补齐功能与边界测试。
#include "framework/test_common.h"

#include "core/BitVector.h"
#include "core/MathUtils.h"
#include "core/MemoryArena.h"
#include "core/ObjectPool.h"
#include "core/PoolAllocator.h"
#include "core/RingBuffer.h"
#include "core/Vector3.h"

#include <cstdint>

TEST_CASE("Memory.Arena")
{
    using BigHero::Core::MemoryArena;
    MemoryArena arena(4096);
    CHECK(arena.ChunkCount() == 0);
    CHECK(arena.TotalAllocated() == 0);

    // 首次分配会开一块。
    void* a = arena.Allocate(16);
    CHECK(a != nullptr);
    CHECK(arena.ChunkCount() == 1);
    CHECK(arena.TotalAllocated() >= 4096);

    // 对齐分配：返回地址必须满足请求对齐。
    void* b = arena.AllocateAligned(64, 64);
    CHECK(reinterpret_cast<uintptr_t>(b) % 64 == 0);

    // 写入并读回。
    auto* ip = static_cast<int*>(arena.AllocateAligned(sizeof(int), alignof(int)));
    *ip = 12345;
    CHECK(*ip == 12345);

    // placement 构造。
    auto* s = arena.Emplace<int>(99);
    CHECK(*s == 99);

    // SaveMark / Rewind：回滚后同一块可复用。
    MemoryArena::Mark m = arena.SaveMark();
    const size_t chunksBefore = arena.ChunkCount();
    (void)arena.Allocate(100000); // 触发新块
    CHECK(arena.ChunkCount() > chunksBefore);
    arena.Rewind(m);
    CHECK(arena.ChunkCount() == chunksBefore);

    // Reset 后各块 offset 归零，内存复用（块数不变）。
    const size_t chunksAtReset = arena.ChunkCount();
    arena.Reset();
    CHECK(arena.ChunkCount() == chunksAtReset);
    (void)arena.Allocate(8);
    CHECK(arena.ChunkCount() == chunksAtReset); // 复用现有块，不新增
}

TEST_CASE("Memory.PoolAllocator")
{
    using BigHero::Core::PoolAllocator;
    PoolAllocator pool(32, 4);
    CHECK(pool.Capacity() == 4);
    CHECK(pool.BlockSize() >= 32);
    CHECK(pool.FreeCount() == 4);
    CHECK(pool.AllocatedCount() == 0);
    CHECK(pool.Empty());
    CHECK(!pool.Full());

    void* p0 = pool.Allocate();
    void* p1 = pool.Allocate();
    void* p2 = pool.Allocate();
    void* p3 = pool.Allocate();
    CHECK(p0 && p1 && p2 && p3);
    CHECK(pool.AllocatedCount() == 4);
    CHECK(pool.FreeCount() == 0);
    CHECK(pool.Full());

    // 容量耗尽返回 nullptr。
    CHECK(pool.Allocate() == nullptr);

    // 释放后归还，可再次分配。
    pool.Free(p1);
    CHECK(pool.FreeCount() == 1);
    void* reused = pool.Allocate();
    CHECK(reused == p1); // LIFO 复用同一块
    CHECK(pool.Full());

    // 块之间不重叠，且在池缓冲范围内。
    CHECK(p0 != p1 && p1 != p2 && p2 != p3);
    // 释放空指针是安全的 no-op。
    pool.Free(nullptr);
    CHECK(pool.FreeCount() == 0);
}

TEST_CASE("Memory.ObjectPool")
{
    bighero::ObjectPool<int> pool(2);
    CHECK(pool.FreeCount() == 2);
    CHECK(!pool.Empty());

    int a = pool.Acquire();
    CHECK(pool.FreeCount() == 1);
    (void)a;
    int b = pool.Acquire();
    CHECK(pool.FreeCount() == 0);
    CHECK(pool.Empty());

    // 池空时 Acquire 返回默认构造对象。
    int c = pool.Acquire();
    CHECK(c == 0);

    // Release 归还（对象被消费，池内存一个默认值）。
    pool.Release(std::move(b));
    CHECK(pool.FreeCount() == 1);
    CHECK(!pool.Empty());

    pool.Clear();
    CHECK(pool.FreeCount() == 0);
    CHECK(pool.Empty());
}

TEST_CASE("Memory.RingBuffer")
{
    bighero::RingBuffer<int> rb(3);
    CHECK(rb.Capacity() == 3);
    CHECK(rb.Empty());
    CHECK(!rb.Full());

    CHECK(rb.PushBack(1));
    CHECK(rb.PushBack(2));
    CHECK(rb.PushBack(3));
    CHECK(rb.Size() == 3);
    CHECK(rb.Full());
    CHECK(!rb.PushBack(4)); // 满时拒绝

    CHECK(rb.Front() == 1);
    CHECK(rb.Back() == 3);

    int out = 0;
    CHECK(rb.PopFront(out) && out == 1);
    CHECK(rb.Size() == 2);
    CHECK(rb.Front() == 2);
    CHECK(!rb.Full());

    // 环形复用：弹出后再入队占据空位。
    CHECK(rb.PushBack(4));
    CHECK(rb.Size() == 3);
    CHECK(rb[0] == 2 && rb[1] == 3 && rb[2] == 4);

    CHECK(rb.PopFront(out) && out == 2);
    CHECK(rb.PopFront(out) && out == 3);
    CHECK(rb.PopFront(out) && out == 4);
    CHECK(rb.Empty());
    CHECK(!rb.PopFront(out)); // 空时返回 false

    rb.PushBack(9);
    rb.Clear();
    CHECK(rb.Empty());
}

TEST_CASE("Memory.BitVector")
{
    using BigHero::Core::BitVector;
    BitVector bv(130); // 跨 3 个 64 位 word
    CHECK(bv.Size() == 130);
    CHECK(bv.WordCount() == 3);
    CHECK(bv.None());
    CHECK(!bv.Any());
    CHECK(bv.CountSetBits() == 0);

    bv.Set(0);
    bv.Set(64);
    bv.Set(129);
    CHECK(bv.Test(0) && bv.Test(64) && bv.Test(129));
    CHECK(!bv.Test(1));
    CHECK(bv.Any());
    CHECK(bv.CountSetBits() == 3);

    bv.Toggle(0);
    CHECK(!bv.Test(0));
    CHECK(bv.CountSetBits() == 2);
    bv.Clear(64);
    CHECK(bv.CountSetBits() == 1);

    // 越界安全：不崩溃、无副作用。
    bv.Set(9999);
    bv.Clear(9999);
    CHECK(!bv.Test(9999));
    CHECK(bv.CountSetBits() == 1);

    // SetAll 只置满有效位，且不超出 size。
    bv.SetAll();
    CHECK(bv.CountSetBits() == 130);
    CHECK(bv.Test(129));
    bv.ClearAll();
    CHECK(bv.None());

    bv.Resize(5);
    CHECK(bv.Size() == 5);
    CHECK(bv.WordCount() == 1);
    CHECK(bv.None());
}

TEST_CASE("Math.Vector3")
{
    using bighero::Vector3;
    Vector3 a(1, 2, 3);
    Vector3 b(4, 5, 6);

    Vector3 s = a + b;
    CHECK(s.x == 5 && s.y == 7 && s.z == 9);
    Vector3 d = b - a;
    CHECK(d.x == 3 && d.y == 3 && d.z == 3);
    Vector3 m = a * 2.0f;
    CHECK(m.x == 2 && m.y == 4 && m.z == 6);
    Vector3 q = b / 2.0f;
    CHECK(q.x == 2 && q.y == 2.5f && q.z == 3);
    Vector3 n = -a;
    CHECK(n.x == -1 && n.y == -2 && n.z == -3);

    CHECK(a.Dot(b) == 4 + 10 + 18);
    Vector3 c = Vector3(1, 0, 0).Cross(Vector3(0, 1, 0));
    CHECK(c.x == 0 && c.y == 0 && c.z == 1);

    Vector3 v(3, 4, 0);
    CHECK_NEAR(v.Magnitude(), 5.0f, 1e-5f);
    CHECK_NEAR(v.SqrMagnitude(), 25.0f, 1e-5f);
    Vector3 unit = v.Normalized();
    CHECK_NEAR(unit.Magnitude(), 1.0f, 1e-5f);

    // 零向量归一化不应产生 NaN。
    Vector3 zero = Vector3(0, 0, 0).Normalized();
    CHECK(zero.x == 0 && zero.y == 0 && zero.z == 0);

    Vector3 l = Vector3::Lerp(Vector3(0, 0, 0), Vector3(10, 0, 0), 0.25f);
    CHECK_NEAR(l.x, 2.5f, 1e-5f);
    CHECK_NEAR(Vector3::Distance(Vector3(0, 0, 0), Vector3(0, 0, 10)), 10.0f, 1e-5f);
    CHECK(Vector3::Zero().x == 0 && Vector3::One().x == 1);
}

TEST_CASE("Math.Utils")
{
    using bighero::MathUtils;
    CHECK_NEAR(MathUtils::DegToRad(180.0f), MathUtils::PI, 1e-4f);
    CHECK_NEAR(MathUtils::RadToDeg(MathUtils::PI), 180.0f, 1e-3f);

    CHECK(MathUtils::Clamp(5.0f, 0.0f, 10.0f) == 5.0f);
    CHECK(MathUtils::Clamp(-1.0f, 0.0f, 10.0f) == 0.0f);
    CHECK(MathUtils::Clamp(99.0f, 0.0f, 10.0f) == 10.0f);
    CHECK(MathUtils::Clamp(5, 0, 3) == 3);

    CHECK_NEAR(MathUtils::Lerp(0.0f, 10.0f, 0.3f), 3.0f, 1e-5f);
    CHECK_NEAR(MathUtils::SmoothStep(0.0f, 1.0f, 0.5f), 0.5f, 1e-5f);
    CHECK_NEAR(MathUtils::SmoothStep(0.0f, 1.0f, -1.0f), 0.0f, 1e-5f);
    CHECK_NEAR(MathUtils::SmoothStep(0.0f, 1.0f, 2.0f), 1.0f, 1e-5f);

    // Repeat 始终落在 [0, length)。
    CHECK_NEAR(MathUtils::Repeat(7.5f, 3.0f), 1.5f, 1e-5f);
    CHECK_NEAR(MathUtils::Repeat(-0.5f, 3.0f), 2.5f, 1e-5f);
    CHECK(MathUtils::Repeat(1.0f, 0.0f) == 0.0f); // 非法长度安全返回
    CHECK_NEAR(MathUtils::PingPong(4.0f, 3.0f), 2.0f, 1e-5f);

    CHECK(MathUtils::Min(3.0f, 2.0f) == 2.0f);
    CHECK(MathUtils::Max(3, 2) == 3);
    CHECK(MathUtils::Abs(-4) == 4);
    CHECK(MathUtils::Abs(-4.0f) == 4.0f);
    CHECK(MathUtils::Sign(-2.0f) == -1.0f);
    CHECK(MathUtils::Sign(0) == 0);
    CHECK(MathUtils::Square(3.0f) == 9.0f);

    // WrapAngle 归一到 [-PI, PI]。
    CHECK_NEAR(MathUtils::WrapAngle(MathUtils::PI * 3.0f), MathUtils::PI, 1e-3f);
    CHECK_NEAR(MathUtils::WrapAngle(0.0f), 0.0f, 1e-6f);

    // MoveTowards：在预算内到达目标，否则按步长逼近。
    CHECK(MathUtils::MoveTowards(0.0f, 10.0f, 100.0f) == 10.0f);
    CHECK_NEAR(MathUtils::MoveTowards(0.0f, 10.0f, 3.0f), 3.0f, 1e-5f);
    CHECK_NEAR(MathUtils::MoveTowards(10.0f, 0.0f, 3.0f), 7.0f, 1e-5f);
}
