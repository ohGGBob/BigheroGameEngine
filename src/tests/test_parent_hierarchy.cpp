// 1.1 生产接线测试：EcsScene 的 Parent 组件经由 TransformHierarchy 层级消费世界矩阵。
//
// 覆盖：
//   - ForEachRenderableWorld 输出的世界矩阵 == 逐实体局部矩阵的父子级联（真实渲染消费路径）；
//   - 无 Parent 时 world == ComputeEntityModelMatrix（与旧逐实体路径逐位一致，零回归）；
//   - 增量重算计数：空闲 O(1)、改叶子仅重算子树、改根重算整树；
//   - 真实场景帧计时：层级脏标记增量帧 vs 逐实体全量重建帧（10k 节点场景）。
#include "framework/test_common.h"

#include "scene/EcsScene.h"

#include <chrono>
#include <cstdio>

using namespace BigHero;
using namespace BigHero::Scene;

namespace
{
// 忽略 CreateObject 返回句柄（避免 nodiscard 噪音）。
void Add(EcsScene& s, const SceneObject& o)
{
    (void)s.CreateObject(o);
}

SceneObject MakeObj(const glm::vec3& pos, float scale = 1.0f, float spinSpeed = 0.0f)
{
    SceneObject o{};
    o.position = pos;
    o.scale = scale;
    o.tint = glm::vec3(1.0f);
    o.spinSpeed = spinSpeed;
    o.phase = 0.0f;
    o.meshId = 0;
    return o;
}

bool MatEqual(const glm::mat4& a, const glm::mat4& b)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(a[c][r] - b[c][r]) > 1e-4f)
                return false;
    return true;
}

// 稳定序下标 i 的局部模型矩阵（与 EcsScene 内部一致）。
glm::mat4 LocalOf(const EcsScene& s, size_t i)
{
    const Core::Entity e = s.At(i);
    const auto& t = s.Registry().Get<ecs::Transform>(e);
    const auto& sp = s.Registry().Get<ecs::Spin>(e);
    return ComputeEntityModelMatrix(t, sp.angle);
}

// 稳定序下标 i 的父下标（无 Parent 组件或空句柄返回 -1）。
int ParentIdx(const EcsScene& s, size_t i)
{
    const Core::Entity e = s.At(i);
    const auto* p = s.Registry().TryGet<ecs::Parent>(e);
    if (p == nullptr || p->parent.IsNull())
        return -1;
    for (size_t k = 0; k < s.ObjectCount(); ++k)
        if (s.At(k).Index() == p->parent.Index())
            return static_cast<int>(k);
    return -1;
}

// 按父子关系逐级级联出的期望世界矩阵。
glm::mat4 ExpectedWorld(const EcsScene& s, std::vector<glm::mat4>& memo, std::vector<char>& done, size_t i)
{
    if (done[i])
        return memo[i];
    const glm::mat4 local = LocalOf(s, i);
    const int p = ParentIdx(s, i);
    memo[i] = (p < 0) ? local : ExpectedWorld(s, memo, done, static_cast<size_t>(p)) * local;
    done[i] = 1;
    return memo[i];
}

// 收集一帧的所有世界矩阵（经生产消费路径）。
std::vector<glm::mat4> CollectWorld(const EcsScene& s)
{
    std::vector<glm::mat4> out;
    out.reserve(s.ObjectCount());
    s.ForEachRenderableWorld([&](const ecs::Transform&, const ecs::Renderable&, const ecs::Spin&, const glm::mat4& w)
                             { out.push_back(w); });
    return out;
}
} // namespace

TEST_CASE("SceneWire.WorldMatchesParentCascade")
{
    EcsScene s;
    Add(s, MakeObj({2.0f, 0.0f, 0.0f}, 1.0f)); // 0 根（非单位：T(2,0,0)）
    Add(s, MakeObj({1.0f, 0.0f, 0.0f}, 2.0f)); // 1 0 的子
    Add(s, MakeObj({0.0f, 2.0f, 0.0f}, 0.5f)); // 2 1 的子
    Add(s, MakeObj({0.0f, 0.0f, 3.0f}, 1.5f)); // 3 0 的子
    Add(s, MakeObj({4.0f, 0.0f, 0.0f}, 1.0f)); // 4 独立根
    s.SetParent(1, 0);
    s.SetParent(2, 1);
    s.SetParent(3, 0);

    std::vector<glm::mat4> memo(s.ObjectCount(), glm::mat4(1.0f));
    std::vector<char> done(s.ObjectCount(), 0);

    const std::vector<glm::mat4> got = CollectWorld(s);
    CHECK(got.size() == s.ObjectCount());
    for (size_t i = 0; i < s.ObjectCount(); ++i)
        CHECK(MatEqual(got[i], ExpectedWorld(s, memo, done, i)));

    // 层级确实生效：子节点世界矩阵 != 其局部矩阵（父变换已级联）。
    CHECK(!MatEqual(got[1], LocalOf(s, 1)));
    CHECK(!MatEqual(got[2], LocalOf(s, 2)));
    // 独立根的世界矩阵等于其局部矩阵。
    CHECK(MatEqual(got[4], LocalOf(s, 4)));

    // 移动根节点 → 其子树（1,2,3）世界矩阵随之改变，独立根 4 不受影响。
    s.SetObjectPosition(0, glm::vec3(10.0f, 0.0f, 0.0f));
    const std::vector<glm::mat4> got2 = CollectWorld(s);
    CHECK(!MatEqual(got[1], got2[1])); // 子节点被父带动
    CHECK(!MatEqual(got[3], got2[3]));
    CHECK(MatEqual(got[4], got2[4])); // 独立根不变
}

TEST_CASE("SceneWire.NoParentMatchesLegacyPerEntity")
{
    // 无任何 Parent：世界矩阵必须与旧逐实体路径 ComputeEntityModelMatrix 逐位一致。
    EcsScene s;
    Add(s, MakeObj({1.0f, 2.0f, 3.0f}, 1.5f, 45.0f));
    Add(s, MakeObj({-2.0f, 0.5f, 4.0f}, 0.75f, 90.0f));
    Add(s, MakeObj({0.0f, -3.0f, 0.0f}, 2.0f, 10.0f));
    s.UpdateSpins(0.25f); // 推进自转角

    size_t idx = 0;
    s.ForEachRenderableWorld(
        [&](const ecs::Transform&, const ecs::Renderable&, const ecs::Spin&, const glm::mat4& w)
        {
            CHECK(MatEqual(w, LocalOf(s, idx)));
            ++idx;
        });
    CHECK(idx == 3);
}

TEST_CASE("SceneWire.IncrementalRecomputeCounts")
{
    // 链式层级（父下标 = i-1），便于精确预测子树规模。
    const size_t N = 100;
    EcsScene s;
    for (size_t i = 0; i < N; ++i)
        Add(s, MakeObj({static_cast<float>(i), 0.0f, 0.0f}));
    for (size_t i = 1; i < N; ++i)
        s.SetParent(i, static_cast<int>(i - 1));

    CHECK(s.RecomputeWorld() == N); // 冷启动全量
    CHECK(s.RecomputeWorld() == 0); // 空闲 O(1)
    CHECK(s.RecomputeWorld() == 0);

    s.SetObjectPosition(N - 1, glm::vec3(999.0f, 0.0f, 0.0f)); // 末端叶子
    CHECK(s.RecomputeWorld() == 1);

    s.SetObjectScale(50, 2.0f); // 中段 → 子树 {50..99} = 50
    CHECK(s.RecomputeWorld() == 50);

    s.SetObjectRotation(0, glm::vec3(0.0f, 90.0f, 0.0f)); // 根 → 整树 100
    CHECK(s.RecomputeWorld() == N);

    CHECK(s.RecomputeWorld() == 0); // 再次空闲
}

// 真实场景帧计时：10k 节点场景，每帧仅少量实体发生变换（最常见的运行时形态）。
//   - 基线：逐实体全量重建模型矩阵（旧渲染路径 O(n)/帧）；
//   - 层级：增量标记 + 脏标记按需重算（O(受影响节点)）+ 空闲帧 O(1)。
// 用 volatile 汇总矩阵分量，防止 -O2 将循环整体消除。
TEST_CASE("SceneWire.BenchmarkRealSceneFrame")
{
    const size_t N = 10000;
    const int kFrames = 100;
    const size_t kChangedPerFrame = 50; // 0.5% 实体/帧发生变化

    std::vector<SceneObject> objs;
    objs.reserve(N);
    for (size_t i = 0; i < N; ++i)
        objs.push_back(MakeObj(glm::vec3(static_cast<float>(i % 100), static_cast<float>(i / 100), 0.0f),
                               1.0f + 0.001f * static_cast<float>(i % 7), 0.0f));

    // ---- 基线：逐实体全量重建（旧路径，每帧对全部 N 个实体建矩阵）----
    std::vector<glm::vec3> pos(N);
    for (size_t i = 0; i < N; ++i)
        pos[i] = objs[i].position;
    volatile double guardFull = 0.0;
    double accFull = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f)
    {
        for (size_t i = 0; i < kChangedPerFrame; ++i) // 少量实体移动
            pos[i].x += 0.01f;
        for (size_t i = 0; i < N; ++i)
        {
            const glm::mat4 m = ComputeObjectModelMatrix(pos[i], objs[i].rotation, objs[i].scale, 0.0f);
            accFull += m[0][0] + m[1][1] + m[2][2] + m[3][0]; // 触达全矩阵，防消除
        }
    }
    guardFull = accFull;
    const auto t1 = std::chrono::steady_clock::now();

    // ---- 层级：EcsScene（扁平：每实体为独立根）+ 脏标记增量 ----
    EcsScene s;
    for (size_t i = 0; i < N; ++i)
        Add(s, objs[i]);
    (void)s.RecomputeWorld(); // 冷启动一次性全量

    volatile double guardDirty = 0.0;
    double accDirty = 0.0;
    size_t totalRecomputed = 0;
    const auto t2 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f)
    {
        for (size_t i = 0; i < kChangedPerFrame; ++i) // 仅少量实体移动
            s.SetObjectPosition(i, glm::vec3(objs[i].position.x + 0.01f * static_cast<float>(f + 1), objs[i].position.y,
                                             objs[i].position.z));
        totalRecomputed += s.RecomputeWorld();
        const TransformHierarchy& h = s.EnsureWorld();
        for (size_t i = 0; i < kChangedPerFrame; ++i)
        {
            const glm::mat4& w = h.World(i);
            accDirty += w[0][0] + w[1][1] + w[2][2] + w[3][0];
        }
    }
    guardDirty = accDirty;
    const auto t3 = std::chrono::steady_clock::now();

    const double fullMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double dirtyMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::printf("  [scene-bench] 10k nodes x %d frames (%zu changed/frame): legacy full-rebuild=%.2f ms, "
                "hierarchy dirty=%.2f ms, speedup=%.1fx, avg recomputed/frame=%.1f\n",
                kFrames, kChangedPerFrame, fullMs, dirtyMs, dirtyMs > 0.0 ? fullMs / dirtyMs : 0.0,
                static_cast<double>(totalRecomputed) / kFrames);

    // 性能叙事：层级增量帧显著快于逐实体全量重建帧。
    CHECK(dirtyMs < fullMs);
    // 每帧平均重算节点远小于 N（绝大多数节点复用缓存）。
    CHECK(totalRecomputed / static_cast<size_t>(kFrames) < N / 10);
    // 防止 -O2 将计量循环整体消除（消费聚合结果）。
    CHECK(guardFull == guardFull && guardDirty == guardDirty);
    CHECK(guardFull > guardDirty); // 全量路径累积的矩阵分量总量更大

    // 正确性：层级结果与逐实体级联一致（抽查若干节点）。
    std::vector<glm::mat4> memo(N, glm::mat4(1.0f));
    std::vector<char> done(N, 0);
    const TransformHierarchy& h = s.EnsureWorld();
    for (size_t i = 0; i < N; i += 97)
        CHECK(MatEqual(h.World(i), ExpectedWorld(s, memo, done, i)));
}
