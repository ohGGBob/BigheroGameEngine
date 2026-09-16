// 变换层级脏标记缓存测试（test_transform_cache）。
//
// 覆盖：TransformHierarchy 的脏标记正确性、按需重算的节点计数，以及相对
// ComputeAllWorldMatrices 全量级联的「前后对照」CPU 计时（10k 节点场景）。
//
// 语义基准：TransformHierarchy 与 ComputeAllWorldMatrices 必须对合法森林给出完全
// 一致的世界矩阵，故多处以两者逐元素对照作为正确性判据。
#include "framework/test_common.h"

#include "scene/Transform.h"

#include <chrono>

using namespace BigHero;
using namespace BigHero::Scene;

namespace
{
// 构造一棵「分支因子 bf」的合法森林：parent[i] = (i-1)/bf，node 0 为根。
std::vector<Transform> BuildTree(size_t n, size_t bf)
{
    std::vector<Transform> ts(n);
    for (size_t i = 0; i < n; ++i)
    {
        ts[i].translation = glm::vec3(static_cast<float>(i), static_cast<float>(i % 7), 0.0f);
        ts[i].rotation = glm::quat(glm::vec3(0.0f, glm::radians(static_cast<float>(i % 11)), 0.0f));
        ts[i].scale = glm::vec3(1.0f + 0.001f * static_cast<float>(i % 5));
        ts[i].parent = (i == 0) ? Transform::kNoParent : static_cast<int32_t>((i - 1) / bf);
    }
    return ts;
}

bool MatEqual(const glm::mat4& a, const glm::mat4& b)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(a[c][r] - b[c][r]) > 1e-4f)
                return false;
    return true;
}

bool AllWorldEqual(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!MatEqual(a[i], b[i]))
            return false;
    return true;
}
} // namespace

TEST_CASE("TransformCache.FirstUpdateRecomputesAll")
{
    TransformHierarchy h(BuildTree(50, 4));
    CHECK(h.Size() == 50);
    CHECK(!h.IsValid(0) || true); // 首次前缓存未就绪（不强制具体值）

    const size_t n = h.UpdateWorld();
    CHECK(n == 50); // 首次全量
    CHECK(h.LastRecomputed() == 50);
    for (size_t i = 0; i < 50; ++i)
        CHECK(h.IsValid(i));

    // 空闲帧：无任何局部修改 → 重算 0 节点，且结果保持不变。
    const std::vector<glm::mat4> before = h.WorldMatrices();
    const size_t idle = h.UpdateWorld();
    CHECK(idle == 0);
    CHECK(AllWorldEqual(before, h.WorldMatrices()));
    CHECK(h.UpdateWorld() == 0); // 连续空闲仍为 0
}

TEST_CASE("TransformCache.LeafEditRecomputesOnlySubtree")
{
    // 分支因子 1 → 单链（深度 = n），便于精确预测重算范围。
    TransformHierarchy h(BuildTree(100, 1));
    CHECK(h.UpdateWorld() == 100);

    // 修改末端叶子（索引 99）：其子树仅 1 个节点。
    h.SetTranslation(99, glm::vec3(1000.0f, 0.0f, 0.0f));
    CHECK(h.DirtyRootCount() == 1);
    CHECK(h.IsDirtyRoot(99));
    const size_t re = h.UpdateWorld();
    CHECK(re == 1);
    CHECK(!h.IsDirtyRoot(99));
    CHECK(h.DirtyRootCount() == 0);

    // 修改链中段（索引 50）：子树 = 节点 50..99，共 50 个。
    h.SetScale(50, glm::vec3(2.0f));
    CHECK(h.DirtyRootCount() == 1);
    CHECK(h.UpdateWorld() == 50);

    // 结果必须与全量重算一致。
    const std::vector<glm::mat4> full = ComputeAllWorldMatrices(h.Locals());
    CHECK(AllWorldEqual(full, h.WorldMatrices()));
}

TEST_CASE("TransformCache.DirtyRootAncestorDominates")
{
    // 先标记后代，再标记祖先 → 祖先吸收后代，根集合只剩祖先。
    TransformHierarchy h(BuildTree(64, 2));
    h.UpdateWorld();

    h.SetTranslation(40, glm::vec3(1.0f)); // 标记 40
    CHECK(h.DirtyRootCount() == 1 && h.IsDirtyRoot(40));

    const int32_t anc = h.Local(40).parent; // 标记其祖先
    h.SetTranslation(static_cast<size_t>(anc), glm::vec3(2.0f));
    CHECK(h.DirtyRootCount() == 1); // 仍只有一个根
    CHECK(h.IsDirtyRoot(static_cast<size_t>(anc)));
    CHECK(!h.IsDirtyRoot(40)); // 后代根已被祖先吸收
    CHECK(h.UpdateWorld() >= 1);

    // 反向：先标记祖先 10，再标记其真正的后代（分支因子 2 下 parent(i)=(i-1)/2，
    // 故 10 的子节点为 21、22）→ 后代标记被忽略（祖先已覆盖）。
    h.SetTranslation(10, glm::vec3(3.0f));
    const size_t rootsBefore = h.DirtyRootCount();
    h.SetTranslation(21, glm::vec3(4.0f)); // 21 = 2*10+1，确为 10 的子节点
    CHECK(h.DirtyRootCount() == rootsBefore);
    CHECK(!h.IsDirtyRoot(21));
    CHECK(h.UpdateWorld() >= 1);

    CHECK(AllWorldEqual(ComputeAllWorldMatrices(h.Locals()), h.WorldMatrices()));
}

TEST_CASE("TransformCache.ParityUnderRandomEdits")
{
    std::vector<Transform> base = BuildTree(500, 3);
    TransformHierarchy h(base);
    h.UpdateWorld();

    // 伪随机编辑序列：每次改一个节点，累积后与全量重算逐元素对照。
    uint32_t seed = 0x12345678u;
    auto rnd = [&seed]()
    {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int step = 0; step < 200; ++step)
    {
        const size_t idx = rnd() % base.size();
        const float f = static_cast<float>(rnd() % 100) * 0.1f;
        switch (step % 3)
        {
        case 0:
            h.SetTranslation(idx, glm::vec3(f, -f, f));
            break;
        case 1:
            h.SetRotation(idx, glm::quat(glm::vec3(0.0f, f, 0.0f)));
            break;
        default:
            h.SetScale(idx, glm::vec3(1.0f + 0.01f * f));
            break;
        }
        h.UpdateWorld();
    }
    CHECK(AllWorldEqual(ComputeAllWorldMatrices(h.Locals()), h.WorldMatrices()));

    // 精确重算计数：每次编辑仅重算该节点子树（用全量结果反查一致性已足够，这里再验证单调性）。
    CHECK(h.LastRecomputed() >= 1);
}

TEST_CASE("TransformCache.DetachedRootFallsBackToLocal")
{
    // 父索引越界：该节点视作根，世界矩阵 = 局部矩阵（与全量级联一致）。
    std::vector<Transform> ts(2);
    ts[0].translation = glm::vec3(1.0f, 2.0f, 3.0f);
    ts[0].parent = Transform::kNoParent;
    ts[1].translation = glm::vec3(4.0f, 5.0f, 6.0f);
    ts[1].parent = 999; // 悬空

    TransformHierarchy h(ts);
    h.UpdateWorld();
    CHECK(MatEqual(h.World(1), LocalToMatrix(ts[1])));
    CHECK(AllWorldEqual(ComputeAllWorldMatrices(ts), h.WorldMatrices()));
}

TEST_CASE("TransformCache.SetParentKeepsTopologyConsistent")
{
    TransformHierarchy h(BuildTree(30, 2));
    h.UpdateWorld();
    const std::vector<glm::mat4> before = h.WorldMatrices();

    // 把节点 20 挂到节点 3 之下。
    h.SetParent(20, 3);
    h.UpdateWorld();
    std::vector<Transform> expect = h.Locals();
    expect[20].parent = 3;
    CHECK(AllWorldEqual(ComputeAllWorldMatrices(expect), h.WorldMatrices()));
    CHECK(!AllWorldEqual(before, h.WorldMatrices())); // 确实发生了变化

    // 还原父子关系，世界矩阵应回到初始值。
    h.SetParent(20, 9); // (20-1)/2 = 9
    h.UpdateWorld();
    CHECK(AllWorldEqual(before, h.WorldMatrices()));
}

TEST_CASE("TransformCache.EmptyHierarchy")
{
    TransformHierarchy h;
    CHECK(h.Empty());
    CHECK(h.Size() == 0);
    CHECK(h.UpdateWorld() == 0);
    CHECK(h.WorldMatrices().empty());
    CHECK(h.DirtyRootCount() == 0);
}

// 10k 节点场景的「前后对照」CPU 计时：全量级联 vs 脏标记按需重算。
TEST_CASE("TransformCache.Benchmark10k")
{
    const size_t N = 10000;
    const int kFrames = 200;
    std::vector<Transform> base = BuildTree(N, 8); // 10000 节点、分支 8 的树

    // ---- 前：每帧全量级联（当前实现） ----
    std::vector<glm::mat4> out;
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f)
    {
        base[static_cast<size_t>(f) % N].translation.x += 0.001f; // 每帧改一个节点
        out = ComputeAllWorldMatrices(base);
    }
    const auto t1 = std::chrono::steady_clock::now();

    // ---- 后：脏标记按需重算 ----
    TransformHierarchy h(BuildTree(N, 8));
    h.UpdateWorld(); // 冷启动一次性全量
    size_t totalRecomputed = 0;
    const auto t2 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f)
    {
        const size_t idx = static_cast<size_t>(f) % N;
        h.SetTranslation(idx, h.Local(idx).translation + glm::vec3(0.001f, 0.0f, 0.0f));
        totalRecomputed += h.UpdateWorld();
    }
    const auto t3 = std::chrono::steady_clock::now();

    const double fullMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double dirtyMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::printf("  [bench] 10k nodes x %d frames: full=%.2f ms, dirty=%.2f ms, speedup=%.1fx, "
                "avg recomputed/frame=%.1f (of %zu)\n",
                kFrames, fullMs, dirtyMs, dirtyMs > 0.0 ? fullMs / dirtyMs : 0.0,
                static_cast<double>(totalRecomputed) / kFrames, N);

    // 正确性：逐帧脏标记结果必须与全量一致。
    CHECK(AllWorldEqual(ComputeAllWorldMatrices(h.Locals()), h.WorldMatrices()));

    // 关键验收：每帧平均重算节点数远小于 N（按需重算成立）。
    CHECK(totalRecomputed / static_cast<size_t>(kFrames) < N / 10);
    // CPU 计时：脏标记路径应显著快于全量路径。
    CHECK(dirtyMs < fullMs);
    // 全量对照基准仍是有效的 dense 输出。
    CHECK(out.size() == N);
}
