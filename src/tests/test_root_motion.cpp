// 动画根运动（scene/RootMotion.h）：纯逻辑、零 GPU，可离线运行。
// 覆盖本体系位移提取 / 偏航提取（含中间时刻）/ 父链级联 / 水平投影裁决 / 俯仰不产生
// 偏航且偏航与俯仰可组合 / 循环边界语义 / 静止与非法输入零增量 / 确定性。
#include "framework/test_common.h"

#include "scene/RootMotion.h"

using namespace BigHero;
using BigHero::Scene::AnimationPlayer;
using BigHero::Scene::ExtractRootMotionDelta;
using BigHero::Scene::GltfAnimation;
using BigHero::Scene::GltfAnimationChannel;
using BigHero::Scene::GltfAnimationSampler;
using BigHero::Scene::GltfModel;
using BigHero::Scene::RootMotionConfig;
using BigHero::Scene::RootMotionDelta;
using BigHero::Scene::WorldTrsAt;

namespace
{
constexpr float kPi = 3.14159265358979f;

// 合成 N 节点骨架：nodeParents 全 -1（单层），绑定 TRS 单位。
GltfModel MakeSkeleton(int nodes, const std::vector<int32_t>& parents = {})
{
    GltfModel m;
    m.nodeParents.resize(static_cast<size_t>(nodes), -1);
    m.nodeTranslations.assign(static_cast<size_t>(nodes), glm::vec3(0.0f));
    m.nodeRotations.assign(static_cast<size_t>(nodes), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    m.nodeScales.assign(static_cast<size_t>(nodes), glm::vec3(1.0f));
    if (!parents.empty())
        m.nodeParents = parents;
    return m;
}

// 平移通道（LINEAR）采样器。
GltfAnimationSampler TranslationSampler(std::initializer_list<float> times,
                                        std::initializer_list<glm::vec3> keys)
{
    GltfAnimationSampler s;
    s.interpolation = "LINEAR";
    s.times = times;
    s.values.reserve(keys.size());
    for (const glm::vec3& k : keys)
        s.values.emplace_back(k.x, k.y, k.z, 0.0f);
    return s;
}

// 旋转通道（LINEAR）采样器：keys 为四元数（内部按 glTF (x,y,z,w) 存）。
GltfAnimationSampler RotationSampler(std::initializer_list<float> times, std::initializer_list<glm::quat> keys)
{
    GltfAnimationSampler s;
    s.interpolation = "LINEAR";
    s.times = times;
    s.values.reserve(keys.size());
    for (const glm::quat& k : keys)
        s.values.emplace_back(k.x, k.y, k.z, k.w);
    return s;
}

void CheckVecNear(const glm::vec3& v, const glm::vec3& e, float eps)
{
    CHECK_NEAR(v.x, e.x, eps);
    CHECK_NEAR(v.y, e.y, eps);
    CHECK_NEAR(v.z, e.z, eps);
}

// 平移+偏航复合通道：本体系位移与世界偏航的解析值。
TEST_CASE("RootMotion.TranslationAndYaw")
{
    GltfModel m = MakeSkeleton(1);
    GltfAnimation a;
    GltfAnimationSampler st = TranslationSampler({0.0f, 1.0f}, {glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f)});
    GltfAnimationSampler sr =
        RotationSampler({0.0f, 1.0f}, {glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                        glm::angleAxis(kPi * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f))});
    a.samplers.push_back(st);
    a.samplers.push_back(sr);
    a.channels.push_back(GltfAnimationChannel{0, "translation", 0});
    a.channels.push_back(GltfAnimationChannel{0, "rotation", 1});
    m.animations.push_back(a);
    AnimationPlayer player(m, 0);
    CHECK(player.IsValid());

    const RootMotionConfig cfg; // rootNode 0, horizontalOnly
    // 全程 [0,1]：位移 (3,0,0)、偏航 +90°
    const RootMotionDelta full = ExtractRootMotionDelta(m, player, cfg, 0.0f, 1.0f);
    CheckVecNear(full.localDelta, glm::vec3(3.0f, 0.0f, 0.0f), 1e-4f);
    CHECK_NEAR(full.yawRadians, kPi * 0.5f, 1e-4f);
    // 前半程 [0,0.5]：位移 (1.5,0,0)、偏航 +45°
    const RootMotionDelta half = ExtractRootMotionDelta(m, player, cfg, 0.0f, 0.5f);
    CheckVecNear(half.localDelta, glm::vec3(1.5f, 0.0f, 0.0f), 1e-4f);
    CHECK_NEAR(half.yawRadians, kPi * 0.25f, 1e-4f);
    // 世界 TRS 直查：t=1 根节点 = (3,0,0) @ Y90
    glm::vec3 wt;
    glm::quat wr;
    glm::vec3 ws;
    WorldTrsAt(m, player, 0, 1.0f, wt, wr, ws);
    CheckVecNear(wt, glm::vec3(3.0f, 0.0f, 0.0f), 1e-4f);
    const glm::quat expectR = glm::angleAxis(kPi * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK_NEAR(glm::dot(wr, expectR), 1.0f, 1e-4f);
}

// 父链级联：子节点的世界增量继承父节点的动画通道。
TEST_CASE("RootMotion.ParentChainCascade")
{
    GltfModel m = MakeSkeleton(2, {-1, 0}); // 1 的父 = 0
    m.nodeTranslations[1] = glm::vec3(2.0f, 0.0f, 0.0f);
    GltfAnimation a;
    a.samplers.push_back(TranslationSampler({0.0f, 1.0f}, {glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f)}));
    a.channels.push_back(GltfAnimationChannel{0, "translation", 0});
    m.animations.push_back(a);
    AnimationPlayer player(m, 0);

    // 根节点 = 1（子）：世界 p0=(2,0,0)、p1=(5,0,0) → 本体系位移 (3,0,0)
    RootMotionConfig cfg;
    cfg.rootNode = 1;
    const RootMotionDelta d = ExtractRootMotionDelta(m, player, cfg, 0.0f, 1.0f);
    CheckVecNear(d.localDelta, glm::vec3(3.0f, 0.0f, 0.0f), 1e-4f);
    CHECK_NEAR(d.yawRadians, 0.0f, 1e-5f);
}

// 水平投影裁决：horizontalOnly 丢弃垂直位移分量，贴地语义。
TEST_CASE("RootMotion.HorizontalOnlyStripsVertical")
{
    GltfModel m = MakeSkeleton(1);
    GltfAnimation a;
    a.samplers.push_back(TranslationSampler({0.0f, 1.0f}, {glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(3.0f, 5.0f, 0.0f)}));
    a.channels.push_back(GltfAnimationChannel{0, "translation", 0});
    m.animations.push_back(a);
    AnimationPlayer player(m, 0);

    RootMotionConfig grounded; // horizontalOnly = true
    const RootMotionDelta d1 = ExtractRootMotionDelta(m, player, grounded, 0.0f, 1.0f);
    CheckVecNear(d1.localDelta, glm::vec3(3.0f, 0.0f, 0.0f), 1e-4f);

    RootMotionConfig airborne;
    airborne.horizontalOnly = false;
    const RootMotionDelta d2 = ExtractRootMotionDelta(m, player, airborne, 0.0f, 1.0f);
    CheckVecNear(d2.localDelta, glm::vec3(3.0f, 1.0f, 0.0f), 1e-4f);
}

// 静止 / player 无效 / 根节点越界 → 零增量。
TEST_CASE("RootMotion.StationaryAndInvalid")
{
    GltfModel m = MakeSkeleton(1);
    GltfAnimation a; // 无通道
    m.animations.push_back(a);
    AnimationPlayer player(m, 0);
    CHECK(player.IsValid());
    const RootMotionDelta d0 = ExtractRootMotionDelta(m, player, RootMotionConfig{}, 0.0f, 1.0f);
    CheckVecNear(d0.localDelta, glm::vec3(0.0f), 1e-6f);
    CHECK_NEAR(d0.yawRadians, 0.0f, 1e-6f);

    // 无动画的模型：player 无效
    GltfModel empty = MakeSkeleton(1);
    AnimationPlayer badPlayer(empty, 0);
    CHECK(!badPlayer.IsValid());
    const RootMotionDelta d1 = ExtractRootMotionDelta(empty, badPlayer, RootMotionConfig{}, 0.0f, 1.0f);
    CheckVecNear(d1.localDelta, glm::vec3(0.0f), 1e-6f);

    // 根节点越界
    RootMotionConfig oob;
    oob.rootNode = 99;
    const RootMotionDelta d2 = ExtractRootMotionDelta(m, player, oob, 0.0f, 1.0f);
    CheckVecNear(d2.localDelta, glm::vec3(0.0f), 1e-6f);
    CHECK_NEAR(d2.yawRadians, 0.0f, 1e-6f);
}

// 时刻语义：提取器对具体时刻精确求值（不取模）；跨循环边界的帧增量由调用方在
// 时间轴上展开为具体时刻对表达（例：[0.9, 1.1] 拆成 [0.9,1.0] 与 [0.0,0.1] 两段）。
TEST_CASE("RootMotion.ConcreteTimeSemantics")
{
    GltfModel m = MakeSkeleton(1);
    GltfAnimation a;
    a.samplers.push_back(TranslationSampler(
        {0.0f, 0.5f, 1.0f}, {glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.25f, 0.0f, 0.0f)}));
    a.channels.push_back(GltfAnimationChannel{0, "translation", 0});
    m.animations.push_back(a);
    AnimationPlayer player(m, 0);

    // 具体时刻精确求值：t=0.9 → u=0.8 → h=0.4；t=0.1 → u=0.2 → h=0.2：Δ = -0.2
    const RootMotionDelta d = ExtractRootMotionDelta(m, player, RootMotionConfig{}, 0.9f, 0.1f);
    CheckVecNear(d.localDelta, glm::vec3(-0.2f, 0.0f, 0.0f), 1e-4f);
    // 循环终点：t=1.0 精确取末帧姿态（不回绕首帧）——这是与 Sample(loop=true) 的关键差异
    const RootMotionDelta end = ExtractRootMotionDelta(m, player, RootMotionConfig{}, 0.0f, 1.0f);
    CheckVecNear(end.localDelta, glm::vec3(0.25f, 0.0f, 0.0f), 1e-4f);
}

// 俯仰不产生偏航；偏航×俯仰组合时偏航分量仍被正确分离。
TEST_CASE("RootMotion.PitchDoesNotYaw")
{
    GltfModel m = MakeSkeleton(1);
    const glm::quat q0(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::quat qPitch = glm::angleAxis(kPi / 6.0f, glm::vec3(1.0f, 0.0f, 0.0f));     // X +30°
    const glm::quat qYawPitch = glm::angleAxis(kPi * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f)) * qPitch; // Y90·X30
    const glm::quat qPitchYaw = qPitch * glm::angleAxis(kPi * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f)); // X30·Y90

    GltfAnimation aPitch;
    aPitch.samplers.push_back(RotationSampler({0.0f, 1.0f}, {q0, qPitch}));
    aPitch.channels.push_back(GltfAnimationChannel{0, "rotation", 0});
    GltfModel mPitch = m;
    mPitch.animations.push_back(aPitch);
    AnimationPlayer pitchPlayer(mPitch, 0);
    const RootMotionDelta dp = ExtractRootMotionDelta(mPitch, pitchPlayer, RootMotionConfig{}, 0.0f, 1.0f);
    CHECK_NEAR(dp.yawRadians, 0.0f, 1e-4f); // 纯俯仰无偏航

    GltfAnimation aCombo;
    aCombo.samplers.push_back(RotationSampler({0.0f, 1.0f}, {q0, qYawPitch}));
    aCombo.channels.push_back(GltfAnimationChannel{0, "rotation", 0});
    GltfModel mCombo = m;
    mCombo.animations.push_back(aCombo);
    AnimationPlayer comboPlayer(mCombo, 0);
    const RootMotionDelta dc = ExtractRootMotionDelta(mCombo, comboPlayer, RootMotionConfig{}, 0.0f, 1.0f);
    CHECK_NEAR(dc.yawRadians, kPi * 0.5f, 1e-3f); // 偏航分量精确分离（组合顺序无关）

    GltfAnimation aCombo2;
    aCombo2.samplers.push_back(RotationSampler({0.0f, 1.0f}, {q0, qPitchYaw}));
    aCombo2.channels.push_back(GltfAnimationChannel{0, "rotation", 0});
    GltfModel mCombo2 = m;
    mCombo2.animations.push_back(aCombo2);
    AnimationPlayer comboPlayer2(mCombo2, 0);
    const RootMotionDelta dc2 = ExtractRootMotionDelta(mCombo2, comboPlayer2, RootMotionConfig{}, 0.0f, 1.0f);
    CHECK_NEAR(dc2.yawRadians, kPi * 0.5f, 1e-3f);
}

// 确定性：同输入两次提取逐位一致。
TEST_CASE("RootMotion.Determinism")
{
    GltfModel m = MakeSkeleton(1);
    GltfAnimation a;
    a.samplers.push_back(TranslationSampler({0.0f, 1.0f}, {glm::vec3(0.0f), glm::vec3(1.5f, 0.0f, -0.5f)}));
    a.samplers.push_back(RotationSampler(
        {0.0f, 1.0f}, {glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::angleAxis(0.7f, glm::vec3(0.0f, 1.0f, 0.0f))}));
    a.channels.push_back(GltfAnimationChannel{0, "translation", 0});
    a.channels.push_back(GltfAnimationChannel{0, "rotation", 1});
    m.animations.push_back(a);
    AnimationPlayer player(m, 0);

    const RootMotionDelta d1 = ExtractRootMotionDelta(m, player, RootMotionConfig{}, 0.1f, 0.9f);
    const RootMotionDelta d2 = ExtractRootMotionDelta(m, player, RootMotionConfig{}, 0.1f, 0.9f);
    CHECK_EQ(d1.localDelta, d2.localDelta);
    CHECK_NEAR(d1.yawRadians, d2.yawRadians, 0.0f);
}

// 循环模式提取：跨循环边界的帧增量由提取器自动补齐每圈净位移（不再要求调用方手动拆分）。
TEST_CASE("RootMotion.LoopedWrapAround")
{
    GltfModel m = MakeSkeleton(1);
    GltfAnimation a;
    a.samplers.push_back(TranslationSampler({0.0f, 1.0f}, {glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f)}));
    a.channels.push_back(GltfAnimationChannel{0, "translation", 0});
    m.animations.push_back(a);
    AnimationPlayer player(m, 0);

    const RootMotionConfig cfg;
    // 圈内等价：[0.2, 0.7] 与非循环版一致 = (1.5, 0, 0)
    CheckVecNear(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 0.2f, 0.7f).localDelta,
                 glm::vec3(1.5f, 0.0f, 0.0f), 1e-4f);
    // 跨圈补齐：[0.9, 1.1] = 圈内(0.3−2.7) + 单圈净位移 3 = (0.6, 0, 0)
    CheckVecNear(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 0.9f, 1.1f).localDelta,
                 glm::vec3(0.6f, 0.0f, 0.0f), 1e-4f);
    // 两整圈 [0, 2] = 2 × 3 = (6, 0, 0)
    CheckVecNear(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 0.0f, 2.0f).localDelta,
                 glm::vec3(6.0f, 0.0f, 0.0f), 1e-4f);
    // 平移窗口 [0.5, 2.5]：同样跨过两圈 = (6, 0, 0)
    CheckVecNear(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 0.5f, 2.5f).localDelta,
                 glm::vec3(6.0f, 0.0f, 0.0f), 1e-4f);
    // 第二圈内的普通区间 [1.2, 1.7] 仍 = (1.5, 0, 0)
    CheckVecNear(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 1.2f, 1.7f).localDelta,
                 glm::vec3(1.5f, 0.0f, 0.0f), 1e-4f);
    // 退化：t1 <= t0 → 零增量
    CheckVecNear(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 0.5f, 0.5f).localDelta,
                 glm::vec3(0.0f), 1e-6f);
}

// 循环模式偏航：跨圈按「尾段 + 整圈×(loops−1) + 首段」累加。
TEST_CASE("RootMotion.LoopedYawAccumulates")
{
    GltfModel m = MakeSkeleton(1);
    GltfAnimation a;
    a.samplers.push_back(RotationSampler({0.0f, 1.0f},
                                         {glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                          glm::angleAxis(kPi * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f))}));
    a.channels.push_back(GltfAnimationChannel{0, "rotation", 0});
    m.animations.push_back(a);
    AnimationPlayer player(m, 0);

    const RootMotionConfig cfg;
    // 圈内：[0.2, 0.6] = 40% × 90° = 36°
    CHECK_NEAR(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 0.2f, 0.6f).yawRadians,
               kPi * 0.2f, 1e-4f);
    // 跨圈：[0.9, 1.1] = 尾段 9° + 首段 9° = 18°
    CHECK_NEAR(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 0.9f, 1.1f).yawRadians,
               kPi * 0.1f, 1e-4f);
    // 两整圈 [0, 2] = 180°
    CHECK_NEAR(Scene::ExtractRootMotionDeltaLooped(m, player, cfg, 0.0f, 2.0f).yawRadians,
               kPi, 1e-4f);
}
} // namespace