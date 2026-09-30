#pragma once
// 反射探针（Reflection Probes，U2-L2）：把场景局部环境的镜面反射离线烘焙成球谐（SH）
// 辐射亮度系数，运行时按位置选探针、按粗糙度做带通预滤波、按反射方向做 box 视差
// 校正后求值——动态物体也能吃到烘焙级镜面环境（U2-L2 的可离线部分）。
// 核心全在 CPU：仅依赖 LightProbe.h（Sh9 机制）+ glm 与标准库、不触碰 Vulkan；可离线单测。
//
// 背景与动机：
//   1. 漫反射一侧已有 LightProbe.h（SH 辐照度 + 体插值）；但金属/光滑表面要的是
//      「反射方向看的那个环境」——SSR 只有屏幕空间信息（背后/遮挡处全黑），IBL 只有
//      一张无限远的天空（局部墙面/霓虹反射不到）。业界（Unity Reflection Probe /
//      Unreal 的反射捕获）的通用解法：在场景放若干探针，离线把每个探针处的入射
//      辐射亮度积分成 SH，运行时按反射方向查询。
//   2. 一张探针的 SH 要同时服务**任意粗糙度**——本模块采用「存储原始辐射亮度 SH +
//      采样时带通衰减」：粗糙度 α 经 GGX↔Phong 转化（n ≈ 2/α²）作用于每阶 SH 的
//      滤波系数 f_l(α) = exp(-l(l+1)·α²/2)。α=0 时 f_l≡1，锐利还原（L2 截断精度内）；
//      α=1 时 L1/L2 严重衰减，只留环境底色。一颗探针一套系数服务所有粗糙度，
//      无需逐粗糙度烘焙 mip 链。能量不变量：均匀环境（辐射亮度恒 L）下任意粗糙度
//      恒得 L（f_0≡1，Y_00 项不衰减）——这条不变量由单测锁死。
//   3. 视差校正（box projection，Sebastien Lagarde 经典方案）：每颗探针绑一个影响盒，
//      视反射射线与盒面的交点方向（而非探针中心方向）采样——反射像钉在盒壁上，
//      物体在盒内移动时反射不漂移、不大不小。交点由 AABB slab 法求，确定性无需迭代。
//
// 契约：
//   - 探针影响域 = 其 box（包含测试选择候选）；盒内采样点按三轴三角衰减权重混合
//     （中心权重 1、盒壁 0），权重跨探针归一化；无效探针（埋在实体里）不参与。
//   - 采样点不在任何有效探针盒内时，退化到「盒中心距离最近的有效探针」（与
//     LightProbe「最近有效探针」兜底同哲学：平滑、不闪黑）；全空才用回退 SH（默认黑）。
//   - 反射方向与面法线无关（镜面反射的入射贡献同 Vs 无关——烘焙的是辐射亮度，不是
//     照度）；R 为指向反射侧的单位向量。
//   - 未做线程同步：Bake 为写、Sample 为读，约定分别离线与运行时执行。
//   - 序列化为纯文本快照（仿 LightmapBaker::SaveLightmap 先例）：可重现、便于 diff。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/FileSystemUtils.h"
#include "render/LightProbe.h"

namespace BigHero::Render
{
// 一颗反射探针的完整描述/数据：位置 + 影响盒 + 入射辐射亮度 SH（未预滤波）。
// SH 未滤波是本模块与「逐粗糙度烘焙 mip 链」方案的分野——滤波在采样时施加。
struct ReflectionProbe
{
    glm::vec3 position{0.0f};
    glm::vec3 boxMin{-2.0f};
    glm::vec3 boxMax{2.0f};
    Sh9 radianceSh{};  // 原始入射辐射亮度（ProjectRadiance 口径）
    bool valid = true; // false = 埋在几何体内，混合时剔除（防漏光）
};

// 反射探针集合：烘焙 / box 视差校正 / 粗糙度带通预滤波 / 混合采样 / 文本序列化。
class ReflectionProbeSet
{
  public:
    // ---- 数据 ----
    // 清空全部探针（场景重建 / 重新放置时用）。
    void Clear() { probes_.clear(); }

    // 添加探针（boxMin 任一轴 >= boxMax 视为退化，拒绝）。返回探针下标，失败返回 -1。
    int AddProbe(const glm::vec3& position, const glm::vec3& boxMin, const glm::vec3& boxMax, bool valid = true)
    {
        if (boxMin.x >= boxMax.x || boxMin.y >= boxMax.y || boxMin.z >= boxMax.z)
            return -1;
        ReflectionProbe p;
        p.position = position;
        p.boxMin = boxMin;
        p.boxMax = boxMax;
        p.valid = valid;
        probes_.push_back(p);
        return static_cast<int>(probes_.size()) - 1;
    }

    [[nodiscard]] size_t ProbeCount() const { return probes_.size(); }
    [[nodiscard]] const ReflectionProbe* Probe(int index) const
    {
        return (index >= 0 && static_cast<size_t>(index) < probes_.size()) ? &probes_[static_cast<size_t>(index)]
                                                                           : nullptr;
    }
    [[nodiscard]] bool IsProbeValid(int index) const
    {
        const ReflectionProbe* p = Probe(index);
        return p != nullptr && p->valid;
    }

    // 直接写入某探针的辐射亮度 SH（烘焙器想自管投影时用），越界返回 false。
    bool SetProbeSh(int index, const Sh9& radianceSh)
    {
        ReflectionProbe* p = MutableProbe(index);
        if (p == nullptr)
            return false;
        p->radianceSh = radianceSh;
        return true;
    }

    // 全空/全无效时的兜底回退 SH（默认黑；室外通常设天空常量）。
    void SetAmbientFallback(const Sh9& sh) { fallback_ = sh; }
    [[nodiscard]] const Sh9& AmbientFallback() const { return fallback_; }

    // ---- 烘焙 ----
    // radianceFn(position, dir) → 该点朝该方向看到的入射辐射亮度。逐探针 Fibonacci
    // 球面投影成 SH（复用 Sh9::ProjectRadiance，L2 截断、确定性）。valid 保持 Add 时状态。
    template<class RadianceFn> void Bake(RadianceFn&& radianceFn, int sampleCount = 512)
    {
        for (ReflectionProbe& p : probes_)
        {
            p.radianceSh =
                Sh9::ProjectRadiance([&](const glm::vec3& dir) { return radianceFn(p.position, dir); }, sampleCount);
        }
    }

    // ---- 带通预滤波（粗糙度 → SH 阶衰减） ----
    // f_l(α) = exp(-l(l+1)·α²/2)：GGX(α) ↔ Phong(n≈2/α²) 转化下的经典带通近似。
    // α=0 → f≡1（锐利反射）；α→1 → L1/L2 快速衰减（只留底色）。粗糙度钳到 [0,1]。
    [[nodiscard]] static Sh9 PrefilterForRoughness(const Sh9& radiance, float roughness)
    {
        const float a = std::clamp(roughness, 0.0f, 1.0f);
        const float f1 = std::exp(-1.0f * a * a); // l=1: l(l+1)=2, ·α²/2 → α²
        const float f2 = std::exp(-3.0f * a * a); // l=2: l(l+1)=6, ·α²/2 → 3α²
        Sh9 out = radiance;
        out.c[1] *= f1;
        out.c[2] *= f1;
        out.c[3] *= f1;
        for (int k = 4; k < 9; ++k)
            out.c[k] *= f2;
        return out;
    }

    // 归一化反射方向（返回 false = 输入零向量/非有限，调用方回退默认）。
    [[nodiscard]] static bool NormalizeDir(const glm::vec3& v, glm::vec3& out)
    {
        const float l = glm::length(v);
        if (!(l > 1e-8f) || !std::isfinite(l))
            return false;
        out = v / l;
        return true;
    }

    // 盒内三轴三角衰减窗（盒中心 1、盒壁 0），探针混合权重用。
    [[nodiscard]] static float TriWindow(float u)
    {
        const float c = std::clamp(u, 0.0f, 1.0f);
        return 1.0f - std::fabs(c - 0.5f) * 2.0f;
    }

    // ---- box 视差校正（Lagarde） ----
    // p 为世界点（可在盒外，先被钳入盒内），R 为反射方向的单位向量：求 p+tR 与 AABB 的
    // 出口交点（盒内起点版 slab 法：真实出口 = 各轴出口 t 的最小者），返回「交点 - 探针
    // 中心」的归一化方向。p 恰在探针中心时交点无唯一解 → 原方向回退。
    [[nodiscard]] static glm::vec3 ParallaxCorrectedDir(const ReflectionProbe& probe, const glm::vec3& p,
                                                        const glm::vec3& r)
    {
        const glm::vec3 origin = glm::clamp(p, probe.boxMin, probe.boxMax) - probe.position;
        if (glm::dot(origin, origin) < 1e-12f)
            return r; // 采样点即探针中心：无唯一交点，回退原方向

        // slab 法（盒内起点版）：origin 已被钳入盒内，故每轴「朝 R 方向到出口面」的 t 必 ≥ 0，
        // 真实出口 = 各轴 t 的最小者（最先命中的面）；|dir|≈0 的轴永不出口，跳过。
        float tExit = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis)
        {
            const float dir = r[axis];
            if (std::fabs(dir) <= 1e-5f)
                continue;
            const float lo =
                (axis == 0) ? (probe.boxMin.x - probe.position.x)
                            : (axis == 1 ? (probe.boxMin.y - probe.position.y) : (probe.boxMin.z - probe.position.z));
            const float hi =
                (axis == 0) ? (probe.boxMax.x - probe.position.x)
                            : (axis == 1 ? (probe.boxMax.y - probe.position.y) : (probe.boxMax.z - probe.position.z));
            const float target = dir >= 0.0f ? hi : lo;
            const float t = (target - origin[axis]) / dir;
            if (t < tExit)
                tExit = t;
        }
        if (tExit >= std::numeric_limits<float>::max())
            return r;                  // 无出口（方向与盒面平行且点在面上）：回退原方向
        tExit = std::max(tExit, 0.0f); // 浮点滑差防微负
        glm::vec3 hit = origin + r * tExit;
        // 出口点必在首出轴上；其余轴可能有 ≤1e-4 相对量的浮点越面，对归一化方向无实质影响。
        const glm::vec3 corrected = hit;
        const float cl = glm::length(corrected);
        if (!(cl > 1e-8f))
            return r;
        return corrected / cl;
    }

    // ---- 采样 ----
    // 完整链路：候选探针（p 在其盒内、有效）→ 三轴三角衰减权重归一化混合；无候选 →
    // 最近中心的有效探针；全无 → 回退 SH。每颗探针按视差校正后的方向、按粗糙度
    // 滤波求值（Sh9::Evaluate 自带非负钳制）。返回镜面环境辐射亮度。
    [[nodiscard]] glm::vec3 Sample(const glm::vec3& p, const glm::vec3& reflectDir, float roughness) const
    {
        glm::vec3 r = reflectDir;
        if (!NormalizeDir(reflectDir, r))
            return glm::vec3(0.0f); // 零/非法反射方向：无方向可查，按黑处理

        // 盒内候选 + 三角衰减权重
        std::vector<int> inside;
        inside.reserve(probes_.size());
        std::vector<float> weights;
        weights.reserve(probes_.size());
        for (size_t i = 0; i < probes_.size(); ++i)
        {
            const ReflectionProbe& probe = probes_[i];
            if (!probe.valid || p.x < probe.boxMin.x || p.x > probe.boxMax.x || p.y < probe.boxMin.y ||
                p.y > probe.boxMax.y || p.z < probe.boxMin.z || p.z > probe.boxMax.z)
                continue;
            const glm::vec3 ext = probe.boxMax - probe.boxMin;
            const glm::vec3 t = (p - probe.boxMin) / ext;
            const float w = TriWindow(t.x) * TriWindow(t.y) * TriWindow(t.z);
            if (w <= 1e-6f)
                continue;
            inside.push_back(static_cast<int>(i));
            weights.push_back(w);
        }

        if (!inside.empty())
        {
            float wsum = 0.0f;
            for (const float w : weights)
                wsum += w;
            glm::vec3 acc(0.0f);
            for (size_t k = 0; k < inside.size(); ++k)
            {
                const ReflectionProbe& probe = probes_[static_cast<size_t>(inside[k])];
                const glm::vec3 rd = ParallaxCorrectedDir(probe, p, r);
                const Sh9 filtered = PrefilterForRoughness(probe.radianceSh, roughness);
                acc += Sh9::Evaluate(filtered, rd) * (weights[k] / wsum);
            }
            return acc;
        }

        // 兜底：盒中心距离最近的有效探针（与 LightProbe「最近有效探针」同哲学）
        float bestDist = std::numeric_limits<float>::max();
        const ReflectionProbe* nearest = nullptr;
        for (const ReflectionProbe& probe : probes_)
        {
            if (!probe.valid)
                continue;
            const glm::vec3 center = (probe.boxMin + probe.boxMax) * 0.5f;
            const float d = glm::dot(center - p, center - p);
            if (d < bestDist)
            {
                bestDist = d;
                nearest = &probe;
            }
        }
        if (nearest != nullptr)
        {
            const glm::vec3 rd = ParallaxCorrectedDir(*nearest, p, r);
            return Sh9::Evaluate(PrefilterForRoughness(nearest->radianceSh, roughness), rd);
        }
        return Sh9::Evaluate(fallback_, r);
    }

    // ---- 序列化（纯文本快照，仿 LightmapBaker::SaveLightmap） ----
    friend bool SaveReflectionProbes(const ReflectionProbeSet&, const std::string&);
    friend bool LoadReflectionProbes(const std::string&, ReflectionProbeSet&);

  private:
    ReflectionProbe* MutableProbe(int index)
    {
        return (index >= 0 && static_cast<size_t>(index) < probes_.size()) ? &probes_[static_cast<size_t>(index)]
                                                                           : nullptr;
    }
    std::vector<ReflectionProbe> probes_;
    Sh9 fallback_{};
};

namespace detail
{
inline std::string Fmt(float v)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return buf;
}
} // namespace detail

// 序列化：纯文本快照（%.9g 精确保真，可重现、便于 diff）。返回是否成功。
inline bool SaveReflectionProbes(const ReflectionProbeSet& set, const std::string& path)
{
    std::ostringstream ss;
    ss << "bighero-reflprobe 1\n";
    ss << "count " << set.probes_.size() << "\n";
    for (size_t i = 0; i < set.probes_.size(); ++i)
    {
        const ReflectionProbe& p = set.probes_[i];
        ss << "probe " << i << " pos " << detail::Fmt(p.position.x) << " " << detail::Fmt(p.position.y) << " "
           << detail::Fmt(p.position.z) << " min " << detail::Fmt(p.boxMin.x) << " " << detail::Fmt(p.boxMin.y) << " "
           << detail::Fmt(p.boxMin.z) << " max " << detail::Fmt(p.boxMax.x) << " " << detail::Fmt(p.boxMax.y) << " "
           << detail::Fmt(p.boxMax.z) << " valid " << (p.valid ? 1 : 0) << "\n";
        for (int k = 0; k < 9; ++k)
            ss << "sh " << detail::Fmt(p.radianceSh.c[k].x) << " " << detail::Fmt(p.radianceSh.c[k].y) << " "
               << detail::Fmt(p.radianceSh.c[k].z) << "\n";
    }
    return Core::FileSystem::WriteText(path, ss.str());
}

inline bool LoadReflectionProbes(const std::string& path, ReflectionProbeSet& set)
{
    std::string text;
    if (!Core::FileSystem::ReadText(path, text))
        return false;
    std::istringstream ss(text);
    std::string tag;
    int version = 0;
    if (!(ss >> tag >> version) || tag != "bighero-reflprobe" || version != 1)
        return false;

    ReflectionProbeSet out;
    size_t count = 0;
    std::vector<ReflectionProbe> parsed;
    size_t shLines = 0;
    std::string word;
    while (ss >> word)
    {
        if (word == "count")
        {
            if (!(ss >> count))
                return false;
            parsed.resize(count);
        }
        else if (word == "probe")
        {
            size_t idx = 0;
            if (!(ss >> idx) || idx >= count)
                return false;
            ReflectionProbe& p = parsed[idx];
            if (!(ss >> word) || word != "pos" || !(ss >> p.position.x >> p.position.y >> p.position.z >> word) ||
                word != "min" || !(ss >> p.boxMin.x >> p.boxMin.y >> p.boxMin.z >> word) || word != "max" ||
                !(ss >> p.boxMax.x >> p.boxMax.y >> p.boxMax.z >> word) || word != "valid")
                return false;
            int validVal = 0;
            if (!(ss >> validVal) || (validVal != 0 && validVal != 1))
                return false;
            p.valid = validVal == 1;
        }
        else if (word == "sh")
        {
            if (shLines >= count * 9)
                return false;
            const size_t probeIdx = shLines / 9;
            const size_t k = shLines % 9;
            if (!(ss >> parsed[probeIdx].radianceSh.c[k].x >> parsed[probeIdx].radianceSh.c[k].y >>
                  parsed[probeIdx].radianceSh.c[k].z))
                return false;
            ++shLines;
        }
        else
        {
            return false; // 未知字段视为损坏
        }
    }
    if (shLines != count * 9)
        return false;
    for (size_t i = 0; i < count; ++i)
    {
        if (parsed[i].boxMin.x >= parsed[i].boxMax.x || parsed[i].boxMin.y >= parsed[i].boxMax.y ||
            parsed[i].boxMin.z >= parsed[i].boxMax.z)
            return false; // 退化盒整体拒绝（与 AddProbe 口径一致）
    }
    out.probes_ = std::move(parsed);
    set = std::move(out);
    return true;
}

} // namespace BigHero::Render