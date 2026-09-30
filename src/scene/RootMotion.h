#pragma once
// 动画根运动（Root Motion，A2）：把动画 clip 中根节点的位移/朝向变化离线提取为帧间
// 增量，供角色控制器施加到世界变换——动画成为运动来源，杜绝贴地滑步。
// 纯 CPU：复用 AnimationPlayer::Sample 的局部 TRS 求值与 GltfModel 层级；可离线单测。
//
// 背景与动机：
//   Walk/Run 动画的腿部运动携带复杂的地面相对位移：若只播动画，角色原地踏步，位移
//   靠控制器硬推——速度与步频不匹配就是滑步。业界（Unity Apply Root Motion / UE Root
//   Motion）标准解法是把 clip 根节点（**含父链动画**）的帧间增量提取出来，由控制器
//   把增量按当前角色朝向施加到世界位置。本模块即这个提取器，只有纯数据与四元数/
//   向量数学，不涉及任何窗口或 GPU。
//
// 约定：
//   - 「世界 TRS」= 沿 nodeParents 自根向下级联局部 TRS（父节点的动画通道同样生效），
//     级联公式 M = M_parent · T_local · R_local · S_local。
//   - ΔP_local = inv(R(root, t0)) · (P(t1) − P(t0))：本体系位移——控制器应用时乘当前
//     角色朝向即可，与帧率无关、与缓存粒度的世界朝向无关。
//   - ΔYaw = fwd(t0)→fwd(t1) 绕场景 Y 轴的水平转角（fwd = R · (0,0,-1) 前向的 XZ
//     投影），正方向 = 绕 +Y 右手定则（从上方看逆时针）；纯俯仰/滚转不产生偏航；
//     fwd 水平投影退化（竖直）时取 0。
//   - horizontalOnly 时 ΔP_local 丢弃垂直分量（贴地角色保持 grounded）。
//   - 时刻语义：ExtractRootMotionDelta 对调用方给出的具体时刻**精确求值（不取模）**；
//     循环/回绕由 ExtractRootMotionDeltaLooped 承担——内部按 clip 时长自动取模，并补齐
//     每圈净位移/净偏航（跨圈区间无需调用方手动拆分）。循环自洽（首尾姿态连续）仍是
//     clip 制作责任，提取器不掩藏不连续。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "scene/Animation.h"
#include "scene/GltfLoader.h"

namespace BigHero::Scene
{
// 根运动提取配置。
struct RootMotionConfig
{
    int rootNode = 0;           // 根节点下标（提取该节点的世界 TRS 增量）
    bool horizontalOnly = true; // 丢弃 ΔP_local 的垂直分量（贴地）
};

// 一段帧间增量：本体系位移 + 绕场景 Y 轴偏航。
struct RootMotionDelta
{
    glm::vec3 localDelta{0.0f};
    float yawRadians = 0.0f;
};

namespace detail
{
// 沿父链级联出节点 node 的世界 TRS（父节点动画通道一并生效），链长防环上限 = 节点数。
inline void CascadeWorldTrs(const GltfModel& model, const std::vector<glm::vec3>& localT,
                            const std::vector<glm::quat>& localR, const std::vector<glm::vec3>& localS, int node,
                            glm::vec3& outT, glm::quat& outR, glm::vec3& outS)
{
    outT = glm::vec3(0.0f);
    outR = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    outS = glm::vec3(1.0f);

    const int n = static_cast<int>(model.nodeTranslations.size());
    if (node < 0 || node >= n)
        return;
    std::vector<int> chain;
    int cur = node;
    int hops = 0;
    while (cur >= 0 && cur < n && hops <= n)
    {
        chain.push_back(cur);
        cur = model.nodeParents.empty() ? -1 : model.nodeParents[static_cast<size_t>(cur)];
        ++hops;
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        const size_t idx = static_cast<size_t>(*it);
        outT += outR * (outS * localT[idx]);
        outR = outR * localR[idx];
        outS = outS * localS[idx];
    }
}
} // namespace detail

// 求节点 node 在时刻 t 的世界 TRS（player 求值局部 TRS + 父链级联；精确时刻，不取模）。
inline void WorldTrsAt(const GltfModel& model, const AnimationPlayer& player, int node, float t, glm::vec3& outT,
                       glm::quat& outR, glm::vec3& outS)
{
    std::vector<glm::vec3> localT;
    std::vector<glm::quat> localR;
    std::vector<glm::vec3> localS;
    player.Sample(t, false, localT, localR, localS);
    detail::CascadeWorldTrs(model, localT, localR, localS, node, outT, outR, outS);
}

// 提取 [t0, t1] 的根运动增量（时刻精确求值，不取模）。player 无效 / 根节点越界 → 零增量。
[[nodiscard]] inline RootMotionDelta ExtractRootMotionDelta(const GltfModel& model, const AnimationPlayer& player,
                                                            const RootMotionConfig& cfg, float t0, float t1)
{
    RootMotionDelta out;
    if (!player.IsValid() || cfg.rootNode < 0 || cfg.rootNode >= static_cast<int>(model.nodeTranslations.size()))
        return out;

    glm::vec3 p0;
    glm::quat r0;
    glm::vec3 s0;
    WorldTrsAt(model, player, cfg.rootNode, t0, p0, r0, s0);
    glm::vec3 p1;
    glm::quat r1;
    glm::vec3 s1;
    WorldTrsAt(model, player, cfg.rootNode, t1, p1, r1, s1);
    (void)s0;
    (void)s1;

    // 本体系位移（根节点朝向帧），horizontalOnly 丢弃垂直分量。
    const glm::vec3 worldDelta = p1 - p0;
    glm::vec3 local = glm::inverse(r0) * worldDelta;
    if (cfg.horizontalOnly)
        local.y = 0.0f;
    out.localDelta = local;

    // 偏航 = 水平前向（fwd = R·(0,0,-1)）的绕 Y 转角（叉积-点积）。
    const glm::vec3 f0 = r0 * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 f1 = r1 * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec2 h0(f0.x, f0.z);
    const glm::vec2 h1(f1.x, f1.z);
    const float l0 = glm::length(h0);
    const float l1 = glm::length(h1);
    if (!(l0 > 1e-6f) || !(l1 > 1e-6f))
    {
        out.yawRadians = 0.0f; // 水平投影退化（前向竖直）：无可定义偏航
        return out;
    }
    const glm::vec2 u0 = h0 / l0;
    const glm::vec2 u1 = h1 / l1;
    // (u0×u1).y = z0·x1 − x0·z1：绕 +Y 右手定则（从上方看逆时针为正）。
    const float cross = u0.y * u1.x - u0.x * u1.y;
    const float dotv = u0.x * u1.x + u0.y * u1.y;
    out.yawRadians = std::atan2(cross, dotv);
    return out;
}

// 循环模式根运动提取：t0/t1 为循环时间轴上的全局时刻（可越过 clip 时长，亦可为负——负
// 时刻按 floor 语义向过去回绕）。内部按 player.Duration() 自动取模，并补齐每圈净位移/
// 净偏航——调用方不再需要手动拆分跨圈区间（对比 ExtractRootMotionDelta 的精确时刻语义）。
//
// 语义模型：clip 视为周期运动，P(t + D) = P(t) + (P(D) − P(0))——单圈净位移由 clip 数据
// 表达；偏航按「尾段 + 首段 + 整圈 × (loops−1)」累加；位移最终表达回 t0 时刻根节点朝向
// 的本体系。循环自洽（首尾姿态连续）仍是 clip 制作责任：非自洽 clip 的跨圈增量无定义，
// 提取器不掩藏。
// t1 <= t0 / Duration() <= 0 / player 无效 / rootNode 越界 → 零增量。
[[nodiscard]] inline RootMotionDelta ExtractRootMotionDeltaLooped(const GltfModel& model, const AnimationPlayer& player,
                                                                  const RootMotionConfig& cfg, float t0, float t1)
{
    RootMotionDelta out;
    if (!player.IsValid() || cfg.rootNode < 0 || cfg.rootNode >= static_cast<int>(model.nodeTranslations.size()) ||
        !(t1 > t0))
        return out;
    const float duration = player.Duration();
    if (!(duration > 0.0f))
        return out;

    // floor 语义取模：t 恰为整圈倍数时归入下一圈起点（与 Sample(loop=true) 的回绕约定一致）。
    const auto wrap = [duration](float t, int& loopBase)
    {
        loopBase = static_cast<int>(std::floor(t / duration));
        return t - static_cast<float>(loopBase) * duration;
    };
    int base0 = 0;
    int base1 = 0;
    const float wt0 = wrap(t0, base0);
    const float wt1 = wrap(t1, base1);
    const int loops = base1 - base0;

    // 同圈区间与精确版完全等价，直接复用。
    if (loops == 0)
        return ExtractRootMotionDelta(model, player, cfg, wt0, wt1);

    // 跨圈位移 = 圈内位移 + 单圈净位移 × 跨过的整圈数。
    glm::vec3 p0;
    glm::quat r0;
    glm::vec3 s0;
    WorldTrsAt(model, player, cfg.rootNode, wt0, p0, r0, s0);
    glm::vec3 p1;
    glm::quat r1;
    glm::vec3 s1;
    WorldTrsAt(model, player, cfg.rootNode, wt1, p1, r1, s1);
    glm::vec3 pD;
    glm::quat rD;
    glm::vec3 sD;
    WorldTrsAt(model, player, cfg.rootNode, duration, pD, rD, sD);
    glm::vec3 pZ;
    glm::quat rZ;
    glm::vec3 sZ;
    WorldTrsAt(model, player, cfg.rootNode, 0.0f, pZ, rZ, sZ);
    (void)s0;
    (void)r1;
    (void)s1;
    (void)rD;
    (void)sD;
    (void)rZ;
    (void)sZ;

    const glm::vec3 worldDelta = (p1 - p0) + (pD - pZ) * static_cast<float>(loops);
    glm::vec3 local = glm::inverse(r0) * worldDelta;
    if (cfg.horizontalOnly)
        local.y = 0.0f;
    out.localDelta = local;

    // 跨圈偏航 = 尾段 + 首段 + 整圈偏航 × (loops − 1)。
    float yaw = ExtractRootMotionDelta(model, player, cfg, wt0, duration).yawRadians +
                ExtractRootMotionDelta(model, player, cfg, 0.0f, wt1).yawRadians;
    if (loops > 1)
        yaw += static_cast<float>(loops - 1) * ExtractRootMotionDelta(model, player, cfg, 0.0f, duration).yawRadians;
    out.yawRadians = yaw;
    return out;
}

} // namespace BigHero::Scene