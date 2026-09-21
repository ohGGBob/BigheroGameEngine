#pragma once
// LOD Group（多细节层次组）：按「屏幕相对高度」自动选择网格细节档，并支持交叉渐变（U2-R1）。
// 纯 CPU、仅依赖 glm 与标准库、不触碰 Vulkan 对象；可离线单测。
//
// 背景与动机：
//   开放世界 / 城市 / 方块世界这类大场景里，远处物体仍按最高模绘制会带来纯粹的浪费——
//   一个占屏 3 像素的雕像没必要跑 5 万三角形。Unity 的 LODGroup 是业界共识解法：
//   给同一物体准备 N 档网格，用「物体在屏幕上占多高」作为统一判据自动切档。
//   选「屏幕相对高度」而非「距离」的原因是它与分辨率、FOV、物体尺寸都无关，
//   同一套阈值在 720p 与 4K、广角与长焦下表现一致（距离阈值则不然）。
//
// 阈值语义（与 Unity LODGroup 对齐）：
//   档位按质量降序排列，lod[0] 最精细。lod[i].screenRelativeHeight = h_i 且 h_0 > h_1 > ... ：
//     - r >= h_0                 → 档 0
//     - h_i <= r < h_(i-1)       → 档 i        （i >= 1）
//     - r < h_(n-1)              → 剔除（或强制末档，见 SetCullBeyondLast）
//   即每档的阈值是它自己的「使用下界」，与 Unity 面板里 LOD0 占顶部条带一致。
//
// 交叉渐变（Cross-fade）：
//   直接切档会有可见的「 popping 」。fadeTransitionWidth = w ∈ [0,1] 表示本档区间的
//   底部多大比例用于与下一档混合：
//     fadeStart = h_i + w * (h_(i-1) - h_i)      // 区间上界对 i=0 取 1.0
//     r >= fadeStart            → 纯本档（fade = 1）
//     h_i <= r < fadeStart      → 本档权重 t = (r - h_i) / (fadeStart - h_i)，下一档权重 1 - t
//   w = 0 即硬切换。末档无下一档可混，恒为纯档。
//
// 契约：
//   - 空组（Count()==0）恒返回 kCulled（-1），调用方需按「不绘制」处理。
//   - 未做线程同步：Evaluate 只读，可并发；改档位约定在主线程。
//   - 本模块只做「选哪一档 + 混合权重」，不负责网格资源本身（档位以整数索引表达，
//     由上层把它映射到 MeshHandle / 实例批次）。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace BigHero::Render
{
// 单档 LOD 的切换参数。
struct LodLevel
{
    // 本档使用下界：物体屏幕相对高度大于等于该值时使用本档（(0, 1]）。
    float screenRelativeHeight = 1.0f;
    // 交叉渐变宽度：本档区间底部用于与下一档混合的比例，0 = 硬切换，1 = 全程混合。
    float fadeTransitionWidth = 0.0f;
};

// 一次 LOD 求值的完整结果。
struct LodSelection
{
    // 主档索引；kCulled(-1) 表示完全剔除。
    int level = 0;
    // 参与混合的次档索引；-1 表示无混合（纯主档）。次档总是比主档粗一级。
    int fadeLevel = -1;
    // 主档权重 ∈ [0, 1]；次档权重 = 1 - fade。无混合时恒为 1。
    float fade = 1.0f;

    [[nodiscard]] bool IsCulled() const { return level < 0; }
    [[nodiscard]] bool IsBlending() const { return fadeLevel >= 0; }
};

class LodGroup
{
  public:
    // 「已剔除」哨兵档位索引。
    static constexpr int kCulled = -1;
    // 档位数上限（对齐 Unity LODGroup 的 8 档上限，便于 UBO 打包）。
    static constexpr size_t kMaxLevels = 8;

    // ---- 档位编辑 ----
    // 按质量降序追加一档。要求 screenRelativeHeight ∈ (0, 1] 且严格小于上一档
    // （否则后续档位永远不可达，属于配置错误 → 拒绝并保留原状态）。
    // 返回 false：参数越界、档位已满、或阈值不严格递减。
    bool AddLevel(float screenRelativeHeight, float fadeTransitionWidth = 0.0f)
    {
        if (levels_.size() >= kMaxLevels)
            return false;
        if (!(screenRelativeHeight > 0.0f) || screenRelativeHeight > 1.0f)
            return false;
        if (!levels_.empty() && !(screenRelativeHeight < levels_.back().screenRelativeHeight))
            return false;
        LodLevel l;
        l.screenRelativeHeight = screenRelativeHeight;
        l.fadeTransitionWidth = std::clamp(fadeTransitionWidth, 0.0f, 1.0f);
        levels_.push_back(l);
        return true;
    }

    // 整体替换档位表（自动按阈值降序排序；重复阈值视为配置错误 → 拒绝，不改动原状态）。
    bool SetLevels(std::vector<LodLevel> levels)
    {
        if (levels.size() > kMaxLevels)
            return false;
        for (const LodLevel& l : levels)
        {
            if (!(l.screenRelativeHeight > 0.0f) || l.screenRelativeHeight > 1.0f)
                return false;
        }
        std::sort(levels.begin(), levels.end(),
                  [](const LodLevel& a, const LodLevel& b) { return a.screenRelativeHeight > b.screenRelativeHeight; });
        for (size_t i = 1; i < levels.size(); ++i)
        {
            if (levels[i].screenRelativeHeight == levels[i - 1].screenRelativeHeight)
                return false; // 重复阈值：区间退化，语义不明确
        }
        levels_ = std::move(levels);
        for (LodLevel& l : levels_)
            l.fadeTransitionWidth = std::clamp(l.fadeTransitionWidth, 0.0f, 1.0f);
        return true;
    }

    void Clear() { levels_.clear(); }

    // ---- 全局调节 ----
    // LOD Bias：>1 偏向精细档（远处也更清楚），<1 偏向粗糙档（性能优先）。
    // 实际判据为 r * bias（默认 1，钳制到 [0.01, 10]，防止 0 或极端值把整组钉死在一档）。
    void SetBias(float bias) { bias_ = std::clamp(bias, 0.01f, 10.0f); }
    [[nodiscard]] float Bias() const { return bias_; }

    // 最高可用档位（质量上限钳制，画质档位设为「低」时跳过最精细档）。
    // 0 = 全部可用；-1 = 全部可用（默认）。设为 k 表示强制使用索引 >= k 的档。
    void SetMaxLevel(int maxLevel) { maxLevel_ = maxLevel < 0 ? -1 : maxLevel; }
    [[nodiscard]] int MaxLevel() const { return maxLevel_; }

    // 低于末档阈值时是否剔除（Unity 默认剔除）。关闭时强制使用最粗档。
    void SetCullBeyondLast(bool cull) { cullBeyondLast_ = cull; }
    [[nodiscard]] bool CullBeyondLast() const { return cullBeyondLast_; }

    // ---- 求值 ----
    // 只选档、不算混合权重（确定档位后无需渐变的场合，成本最低）。
    [[nodiscard]] int SelectLevel(float relativeHeight) const
    {
        if (levels_.empty())
            return kCulled;
        const float r = relativeHeight * bias_;
        size_t i = 0;
        while (i < levels_.size() && r < levels_[i].screenRelativeHeight)
            ++i;
        if (i >= levels_.size())
        {
            if (!cullBeyondLast_)
                return ClampLevel(static_cast<int>(levels_.size()) - 1);
            return kCulled;
        }
        return ClampLevel(static_cast<int>(i));
    }

    // 完整求值：档位 + 交叉渐变权重。
    [[nodiscard]] LodSelection Evaluate(float relativeHeight) const
    {
        LodSelection out;
        if (levels_.empty())
        {
            out.level = kCulled;
            out.fadeLevel = -1;
            out.fade = 1.0f;
            return out;
        }
        const float r = relativeHeight * bias_;
        // 定位区间：找到首个 h_i <= r 的档（即当前档）。
        size_t i = 0;
        while (i < levels_.size() && r < levels_[i].screenRelativeHeight)
            ++i;

        if (i >= levels_.size())
        {
            // 比最粗档阈值还小：剔除或钳到末档。
            if (cullBeyondLast_)
            {
                out.level = kCulled;
                out.fadeLevel = -1;
                out.fade = 1.0f;
                return out;
            }
            i = levels_.size() - 1;
        }

        // 钳到 MaxLevel 允许的最细档：钳制只会让索引变大（更粗），故 r >= h_level 依然成立。
        const size_t level = static_cast<size_t>(ClampLevel(static_cast<int>(i)));
        out.level = static_cast<int>(level);

        // 交叉渐变：以最终生效的档位为准（含 MaxLevel 钳制的情形），与下一档混合。
        const size_t next = level + 1;
        if (next >= levels_.size())
        {
            out.fadeLevel = -1;
            out.fade = 1.0f;
            return out;
        }
        const float w = levels_[level].fadeTransitionWidth;
        if (w <= 0.0f)
        {
            out.fadeLevel = -1;
            out.fade = 1.0f;
            return out;
        }
        // 区间上界：档 0 取 1.0（屏幕占比上限），其余取上一档阈值。
        const float hi = (level == 0) ? 1.0f : levels_[level - 1].screenRelativeHeight;
        const float lo = levels_[level].screenRelativeHeight;
        const float span = hi - lo;
        if (!(span > 0.0f))
        {
            out.fadeLevel = -1;
            out.fade = 1.0f;
            return out;
        }
        const float fadeStart = lo + w * span;
        if (r >= fadeStart)
        {
            out.fadeLevel = -1;
            out.fade = 1.0f;
            return out;
        }
        const float t = (r - lo) / (fadeStart - lo);
        out.fade = std::clamp(t, 0.0f, 1.0f);
        out.fadeLevel = static_cast<int>(next);
        return out;
    }

    // ---- 屏幕相对高度换算 ----
    // 半径 worldRadius 的包围球在距离 distance 处、垂直半视角正切 tanHalfFovY 下，
    // 占屏幕高度的比例： 2R / (2 * d * tan(fov/2)) = R / (d * tan(fov/2))。
    // distance 非正（物体在相机平面内/背后）时钳到极小值 → 返回大值，必然选最精细档。
    [[nodiscard]] static float ScreenRelativeHeight(float worldRadius, float distance, float tanHalfFovY)
    {
        const float d = std::max(distance, 1e-4f);
        const float t = std::max(tanHalfFovY, 1e-4f);
        return std::clamp(worldRadius / (d * t), 0.0f, 1e6f);
    }

    // 反解：给定物体尺寸与视角，达到某屏幕相对高度所需的距离（面板上显示「档位切换距离」用）。
    [[nodiscard]] static float DistanceForScreenRelativeHeight(float worldRadius, float relativeHeight,
                                                               float tanHalfFovY)
    {
        const float r = std::max(relativeHeight, 1e-6f);
        const float t = std::max(tanHalfFovY, 1e-4f);
        return worldRadius / (r * t);
    }

    // 各档的切换距离（对应每档阈值），供 Inspector 显示与「按距离回退」的调试视图使用。
    [[nodiscard]] std::vector<float> DistanceThresholds(float worldRadius, float tanHalfFovY) const
    {
        std::vector<float> out;
        out.reserve(levels_.size());
        for (const LodLevel& l : levels_)
            out.push_back(DistanceForScreenRelativeHeight(worldRadius, l.screenRelativeHeight, tanHalfFovY));
        return out;
    }

    // ---- 只读访问 ----
    [[nodiscard]] size_t Count() const { return levels_.size(); }
    [[nodiscard]] const std::vector<LodLevel>& Levels() const { return levels_; }

  private:
    // 把档位索引钳到 [maxLevel_, n-1]：maxLevel_ 为 -1/0 时不动。
    [[nodiscard]] int ClampLevel(int level) const
    {
        if (maxLevel_ <= 0)
            return level;
        // 上限也不能越界：MaxLevel 设超过档位数时钳到末档，绝不返回非法索引。
        const int lo = std::min(maxLevel_, static_cast<int>(levels_.size()) - 1);
        return std::max(level, lo);
    }

    std::vector<LodLevel> levels_;
    float bias_ = 1.0f;
    int maxLevel_ = -1;
    bool cullBeyondLast_ = true;
};
} // namespace BigHero::Render
