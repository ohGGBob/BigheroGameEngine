// 反射探针（render/ReflectionProbe.h）：纯逻辑、零 GPU，可离线运行。
// 覆盖均匀环境能量不变量 / 全链路与直评 SH 的自洽性 / 粗糙度带通衰减 / box 视差
// 校正几何（含退出轴选择与盒外钳制）/ 多探针三角窗混合 / 无效剔除 / 最近探针兜底 /
// 退化输入拒绝 / 文本序列化往返与损坏拒绝。
#include "framework/test_common.h"

#include "render/ReflectionProbe.h"

using namespace BigHero;
using BigHero::Render::LoadReflectionProbes;
using BigHero::Render::ReflectionProbe;
using BigHero::Render::ReflectionProbeSet;
using BigHero::Render::SaveReflectionProbes;
using BigHero::Render::Sh9;

namespace
{
// 均匀环境（辐射亮度恒 L）的 SH：c_0 = L·2√π，其余阶为 0。
Sh9 UniformSh(const glm::vec3& l)
{
    Sh9 s;
    s.c[0] = l * 2.0f * std::sqrt(3.14159265358979f);
    return s;
}

void CheckVecNear(const glm::vec3& v, const glm::vec3& e, float eps)
{
    CHECK_NEAR(v.x, e.x, eps);
    CHECK_NEAR(v.y, e.y, eps);
    CHECK_NEAR(v.z, e.z, eps);
}

// 均匀环境不变量：任意位置/方向/粗糙度恒得 L（f_0≡1 且 c_0 项与方向无关）。
// 容差口径：512 样本 Fibonacci 浮点投影常数函数有 ~2e-4 相对量的数值累加误差，
// 5e-4（千分之五）仍把能量守恒锁得足够紧。
TEST_CASE("ReflProbe.UniformEnvInvariant")
{
    ReflectionProbeSet set;
    set.AddProbe(glm::vec3(0.0f), glm::vec3(-2.0f), glm::vec3(2.0f));
    set.AddProbe(glm::vec3(6.0f, 0.0f, 0.0f), glm::vec3(4.0f, -2.0f, -2.0f), glm::vec3(8.0f, 2.0f, 2.0f));
    set.Bake([](const glm::vec3&, const glm::vec3&) { return glm::vec3(1.0f, 0.5f, 0.25f); });

    const glm::vec3 env(1.0f, 0.5f, 0.25f);
    const glm::vec3 dirs[] = {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                              glm::vec3(0.0f, 0.0f, -1.0f), glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f))};
    const float roughs[] = {0.0f, 0.25f, 0.5f, 1.0f};
    const glm::vec3 points[] = {glm::vec3(0.0f), glm::vec3(1.0f, -1.0f, 0.5f), glm::vec3(7.0f, 1.0f, -1.0f)};
    for (const glm::vec3& p : points)
        for (const glm::vec3& d : dirs)
            for (const float a : roughs)
                CheckVecNear(set.Sample(p, d, a), env, 5e-4f);
}

// 全链路自洽：探针中心处 Sample 必须与「投影 → 预滤波 → 直接求值」参考完全一致。
TEST_CASE("ReflProbe.PipelineSelfConsistency")
{
    const glm::vec3 kSun = glm::normalize(glm::vec3(1.0f, 0.4f, -0.3f));
    const glm::vec3 kColor(0.2f, 0.8f, 0.3f);
    auto lobe = [&](const glm::vec3&, const glm::vec3& dir) {
        return kColor * std::max(glm::dot(dir, kSun), 0.0f);
    };

    ReflectionProbeSet set;
    set.AddProbe(glm::vec3(0.0f), glm::vec3(-2.0f), glm::vec3(2.0f));
    set.Bake(lobe, 512);

    const Sh9 ref = Sh9::ProjectRadiance([&](const glm::vec3& d) { return lobe(glm::vec3(0.0f), d); }, 512);

    const glm::vec3 dirs[] = {kSun, -kSun, glm::vec3(1.0f, 0.0f, 0.0f), glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f))};
    const float roughs[] = {0.0f, 0.5f, 1.0f};
    for (const glm::vec3& d : dirs)
        for (const float a : roughs)
        {
            const glm::vec3 expected = Sh9::Evaluate(ReflectionProbeSet::PrefilterForRoughness(ref, a), d);
            const glm::vec3 got = set.Sample(glm::vec3(0.0f), d, a); // 探针中心：权重 1、无视差
            CheckVecNear(got, expected, 1e-4f);
        }
}

// 粗糙度带通衰减：锥形瓣环境在太阳方向 α=1 的镜面亮度显著低于 α=0（能量向远处摊开）。
TEST_CASE("ReflProbe.RoughnessBlursLobe")
{
    const glm::vec3 kSun(1.0f, 0.0f, 0.0f);
    ReflectionProbeSet set;
    set.AddProbe(glm::vec3(0.0f), glm::vec3(-2.0f), glm::vec3(2.0f));
    set.Bake(
        [&](const glm::vec3&, const glm::vec3& dir) {
            return glm::vec3(1.0f) * std::max(glm::dot(dir, kSun), 0.0f);
        },
        512);
    const float sharp = set.Sample(glm::vec3(0.0f), kSun, 0.0f).x;
    const float rough = set.Sample(glm::vec3(0.0f), kSun, 1.0f).x;
    CHECK(sharp > rough + 0.1f); // L1 衰减 exp(-1)≈0.368，镜面峰值必然显著下降
}

// box 视差校正几何：中心回退 / 轴向出口 / 首出轴为最先命中的面 / 盒外钳制。
TEST_CASE("ReflProbe.ParallaxGeometry")
{
    ReflectionProbe probe;
    probe.position = glm::vec3(0.0f);
    probe.boxMin = glm::vec3(-1.0f);
    probe.boxMax = glm::vec3(1.0f);

    // 探针中心：无唯一交点 → 原方向
    CheckVecNear(ReflectionProbeSet::ParallaxCorrectedDir(probe, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
                 glm::vec3(0.0f, 0.0f, 1.0f), 1e-5f);
    // 靠近 +X 面朝 +X：出口 +X 面 → 方向 +X
    CheckVecNear(ReflectionProbeSet::ParallaxCorrectedDir(probe, glm::vec3(0.9f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                 glm::vec3(1.0f, 0.0f, 0.0f), 1e-5f);
    // 同一位置朝 -X：出口 -X 面
    CheckVecNear(ReflectionProbeSet::ParallaxCorrectedDir(probe, glm::vec3(0.9f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)),
                 glm::vec3(-1.0f, 0.0f, 0.0f), 1e-5f);
    // 东北向：x 面先到（t=0.707 < y 的 1.414）→ 交点 (1, 0.5, 0)
    const glm::vec3 ne = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    const glm::vec3 got = ReflectionProbeSet::ParallaxCorrectedDir(probe, glm::vec3(0.5f, 0.0f, 0.0f), ne);
    CheckVecNear(got, glm::normalize(glm::vec3(1.0f, 0.5f, 0.0f)), 1e-4f);
    // 盒外点：钳入盒面起算；p=(3,0,0) 朝 -X 出口仍为 -X 面
    CheckVecNear(ReflectionProbeSet::ParallaxCorrectedDir(probe, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)),
                 glm::vec3(-1.0f, 0.0f, 0.0f), 1e-5f);
}

// 多探针混合：三轴三角窗权重归一化；两盒重叠中点 = 均值；无效探针剔除。
TEST_CASE("ReflProbe.BlendAndPruneInvalid")
{
    ReflectionProbeSet set;
    set.AddProbe(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(-1.5f, -1.0f, -1.0f), glm::vec3(0.5f, 1.0f, 1.0f));
    set.AddProbe(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-0.5f, -1.0f, -1.0f), glm::vec3(1.5f, 1.0f, 1.0f));
    set.SetProbeSh(0, UniformSh(glm::vec3(1.0f, 0.0f, 0.0f)));
    set.SetProbeSh(1, UniformSh(glm::vec3(0.0f, 0.0f, 1.0f)));

    // A 中心（仅 A 盒内）→ 纯红
    CheckVecNear(set.Sample(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.3f),
                 glm::vec3(1.0f, 0.0f, 0.0f), 1e-4f);
    // 重叠中点 (0,0,0)：两侧三角窗各 0.5，归一化后 = 均值 (0.5, 0, 0.5)
    CheckVecNear(set.Sample(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.3f),
                 glm::vec3(0.5f, 0.0f, 0.5f), 1e-4f);

    // 把 B 标记无效：中点只剩 A → 纯红
    ReflectionProbe* b = const_cast<ReflectionProbe*>(set.Probe(1));
    CHECK(b != nullptr);
    b->valid = false;
    CheckVecNear(set.Sample(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.3f),
                 glm::vec3(1.0f, 0.0f, 0.0f), 1e-4f);
}

// 兜底：盒外采样点取盒中心最近的有效探针；全无效/空集合 → 回退 SH（默认黑）。
TEST_CASE("ReflProbe.NearestFallbackAndRejects")
{
    ReflectionProbeSet set;
    set.AddProbe(glm::vec3(-3.0f, 0.0f, 0.0f), glm::vec3(-4.0f, -1.0f, -1.0f), glm::vec3(-2.0f, 1.0f, 1.0f));
    set.AddProbe(glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(2.0f, -1.0f, -1.0f), glm::vec3(4.0f, 1.0f, 1.0f));
    set.SetProbeSh(0, UniformSh(glm::vec3(1.0f, 0.0f, 0.0f)));
    set.SetProbeSh(1, UniformSh(glm::vec3(0.0f, 0.0f, 1.0f)));

    CheckVecNear(set.Sample(glm::vec3(5.5f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.5f),
                 glm::vec3(0.0f, 0.0f, 1.0f), 1e-4f); // 最近 B
    CheckVecNear(set.Sample(glm::vec3(-5.5f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.5f),
                 glm::vec3(1.0f, 0.0f, 0.0f), 1e-4f); // 最近 A
    // 等距决胜确定性：中点 (0,0,0) 离 A/B 盒中心等距 → 取先登记者 A（红）
    CheckVecNear(set.Sample(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.5f),
                 glm::vec3(1.0f, 0.0f, 0.0f), 1e-4f);

    // 退化输入
    CHECK_EQ(set.AddProbe(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.0f)), -1); // min >= max
    CHECK(!set.SetProbeSh(99, Sh9{}));
    CHECK_EQ(set.Probe(99), nullptr);
    CHECK(!set.IsProbeValid(99));
    CHECK(!set.IsProbeValid(-1));

    // 空集合：一切采样皆回退黑
    ReflectionProbeSet empty;
    CheckVecNear(empty.Sample(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.0f), glm::vec3(0.0f), 0.0f);
}

// 文本序列化：往返逐位一致 + 采样值一致；损坏/截断/非法字段整体拒绝。
TEST_CASE("ReflProbe.Serialization")
{
    ReflectionProbeSet set;
    set.AddProbe(glm::vec3(-3.0f, 0.0f, 0.0f), glm::vec3(-4.0f, -1.0f, -1.0f), glm::vec3(-2.0f, 1.0f, 1.0f));
    set.AddProbe(glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(2.0f, -1.0f, -1.0f), glm::vec3(4.0f, 1.0f, 1.0f));
    set.AddProbe(glm::vec3(0.0f, 8.0f, 0.0f), glm::vec3(-1.0f, 7.0f, -1.0f), glm::vec3(1.0f, 9.0f, 1.0f), false);
    set.SetProbeSh(0, UniformSh(glm::vec3(1.0f, 0.0f, 0.0f)));
    set.SetProbeSh(1, UniformSh(glm::vec3(0.0f, 0.0f, 1.0f)));
    set.SetProbeSh(2, UniformSh(glm::vec3(0.1f, 0.9f, 0.4f)));

    Core::FileSystem::CreateDirs("out");
    const std::string path = "out/bh_reflprobe_roundtrip.rp";
    CHECK(SaveReflectionProbes(set, path));
    ReflectionProbeSet loaded;
    CHECK(LoadReflectionProbes(path, loaded));
    CHECK_EQ(loaded.ProbeCount(), set.ProbeCount());
    for (size_t i = 0; i < set.ProbeCount(); ++i)
    {
        const ReflectionProbe* a = set.Probe(static_cast<int>(i));
        const ReflectionProbe* b = loaded.Probe(static_cast<int>(i));
        CHECK_EQ(b->valid, a->valid);
        CHECK_EQ(b->position, a->position);
        CHECK_EQ(b->boxMin, a->boxMin);
        CHECK_EQ(b->boxMax, a->boxMax);
        for (int k = 0; k < 9; ++k)
            CHECK_EQ(b->radianceSh.c[k], a->radianceSh.c[k]); // %.9g 十进制往返保真
    }
    // 采样一致性（含就近兜底点）
    const glm::vec3 probes[] = {glm::vec3(-3.0f, 0.0f, 0.0f), glm::vec3(5.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)};
    for (const glm::vec3& p : probes)
        CheckVecNear(loaded.Sample(p, glm::vec3(0.0f, 1.0f, 0.0f), 0.4f),
                     set.Sample(p, glm::vec3(0.0f, 1.0f, 0.0f), 0.4f), 1e-6f);

    // 损坏输入整体拒绝
    std::vector<std::string> corrupted = {
        "garbage",
        "bighero-reflprobe 2\ncount 1\n",
        "bighero-reflprobe 1\ncount 1\nbogus 0\n",
        "bighero-reflprobe 1\ncount 1\nprobe 0 pos 0 0 0 min -1 -1 -1 max 1 1 1 valid 2\n",
        "bighero-reflprobe 1\ncount 1\nprobe 0 pos 0 0 0 min -1 -1 -1 max 1 1 1 valid 1\nsh 1 0 0\n",
        "bighero-reflprobe 1\ncount 1\nprobe 0 pos 0 0 0 min 1 1 1 max -1 -1 -1 valid 1\n"};
    for (size_t i = 0; i < corrupted.size(); ++i)
    {
        const std::string bad = "out/bh_reflprobe_bad" + std::to_string(i) + ".rp";
        Core::FileSystem::WriteText(bad, corrupted[i]);
        ReflectionProbeSet reject;
        CHECK(!LoadReflectionProbes(bad, reject));
    }
    ReflectionProbeSet missing;
    CHECK(!LoadReflectionProbes("out/bh_reflprobe_no_such.rp", missing));
}
} // namespace