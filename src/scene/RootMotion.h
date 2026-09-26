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
//   - 时刻语义：提取器对调用方给出的具体时刻**精确求值（不取模）**——循环/回绕策略
//     属于时间轴层：跨循环边界的帧增量请由调用方把时间轴展开为末段时刻后表达
//     （例如 [0.9, 1.0] 与 [0.0, 0.1] 两段分别提取）。循环自洽（首尾姿态连续）是
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
    int rootNode = 0;         // 根节点下标（提取该节点的世界 TRS 增量）
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
    if (!player.IsValid() || cfg.rootNode < 0 ||
        cfg.rootNode >= static_cast<int>(model.nodeTranslations.size()))
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

} // namespace BigHero::Scene