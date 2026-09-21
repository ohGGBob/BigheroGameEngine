#pragma once
// 展示厅运行时子系统（纯逻辑，无 GPU 资源）：第一人称「特性展台」体验层的状态机。
//
// 职责：
//   - 持有赛博城市场景导出的展台清单 / 出生点 / 霓虹灯参数；
//   - 每帧按「眼位 + 视线方向」解算当前注视的展台（准星交互用）；
//   - 时段预设（黄昏 / 夜晚 / 矩阵蓝 / 正午）循环与查询；
//   - 已体验特性打点（HUD 进度用）。
//
// 与 Application 的分工：本类只做数据查询与状态推进，特性开关与渲染 / 后处理参数
// 仍由 Application（PostProcessSync 等）持有；HUD 绘制经本类查询结果在 RecordUi 内完成。

#include "showcase/CyberCity.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace BigHero
{
class ShowcaseHost
{
  public:
    // 展台准星判定的可调参数（公有，便于编辑器/命令行微调）
    float focusMaxDistance = 22.0f;  // 超过该距离不再触发交互提示
    float focusCosThreshold = 0.90f; // 视线夹角余弦阈值（约 26°）
    float exhibitFocusHeight = 3.0f; // 展台注视点高度（悬浮展示物所在高度）

    void Load(const Sample::Showcase::CyberCityBuild& build)
    {
        exhibits_ = build.exhibits;
        spawn_ = build.spawn;
        spawnYaw_ = build.spawnYaw;
        active_ = !exhibits_.empty();
        focusIndex_ = -1;
        focusDistance_ = 0.0f;
        visitedMask_ = 0;
    }

    void Reset() noexcept
    {
        focusIndex_ = -1;
        focusDistance_ = 0.0f;
        visitedMask_ = 0;
        dayTime_ = kDefaultDayTime;
        targetDayTime_ = kDefaultDayTime;
        autoCycle_ = false;
        lookIndex_ = 0;
    }

    [[nodiscard]] bool Active() const noexcept { return active_; }
    void SetActive(bool v) noexcept { active_ = v; }

    // 每帧解算注视展台：优先取视线夹角最小者（距离阈值内）
    void Update(const glm::vec3& eye, const glm::vec3& forward) noexcept
    {
        focusIndex_ = -1;
        focusDistance_ = 0.0f;
        if (!active_)
            return;

        const glm::vec3 dir = (glm::length(forward) > 1e-5f) ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);
        float bestCos = focusCosThreshold;
        for (std::size_t i = 0; i < exhibits_.size(); ++i)
        {
            const glm::vec3 target = exhibits_[i].position + glm::vec3(0.0f, exhibitFocusHeight, 0.0f);
            const glm::vec3 to = target - eye;
            const float dist = glm::length(to);
            if (dist > focusMaxDistance || dist < 1e-4f)
                continue;
            const float cosA = glm::dot(to / dist, dir);
            if (cosA > bestCos)
            {
                bestCos = cosA;
                focusIndex_ = static_cast<int>(i);
                focusDistance_ = dist;
            }
        }
    }

    [[nodiscard]] int FocusedIndex() const noexcept { return focusIndex_; }
    [[nodiscard]] float FocusDistance() const noexcept { return focusDistance_; }

    [[nodiscard]] const Sample::Showcase::CyberExhibit* Focused() const noexcept
    {
        if (focusIndex_ < 0 || focusIndex_ >= static_cast<int>(exhibits_.size()))
            return nullptr;
        return &exhibits_[static_cast<std::size_t>(focusIndex_)];
    }

    // 按特性 id 查找展台（数字键直达用；找不到返回 nullptr）
    [[nodiscard]] const Sample::Showcase::CyberExhibit* FindByFeature(int featureId) const noexcept
    {
        for (const Sample::Showcase::CyberExhibit& e : exhibits_)
        {
            if (e.featureId == featureId)
                return &e;
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<Sample::Showcase::CyberExhibit>& Exhibits() const noexcept { return exhibits_; }
    [[nodiscard]] const glm::vec3& Spawn() const noexcept { return spawn_; }
    [[nodiscard]] float SpawnYaw() const noexcept { return spawnYaw_; }

    // ---- 时段（连续昼夜：dayTime_ 在预设环上平滑推进，整数位=锚点，小数位=过渡进度） ----
    void CycleTimeOfDay() noexcept { SetTimeOfDay(TimeOfDayIndex() + 1); }

    void SetTimeOfDay(int index) noexcept
    {
        const auto n = static_cast<int>(Sample::Showcase::CyberCityTimePresets().size());
        if (n <= 0)
            return;
        const int wrapped = ((index % n) + n) % n;
        // 沿环走最短路径：目标取"当前锚点 + 最短有向步"，避免 3→0 时倒着穿过全部预设
        const int cur = static_cast<int>(std::floor(dayTime_ + 0.5f)) % n;
        int delta = wrapped - cur;
        if (delta > n / 2)
            delta -= n;
        if (delta < -n / 2)
            delta += n;
        targetDayTime_ = static_cast<float>(cur + delta);
    }

    [[nodiscard]] int TimeOfDayIndex() const noexcept
    {
        const auto n = static_cast<int>(Sample::Showcase::CyberCityTimePresets().size());
        if (n <= 0)
            return 0;
        const int t = static_cast<int>(std::floor(targetDayTime_ + 0.5f)) % n;
        return (t + n) % n;
    }

    // 每帧推进：自动循环模式下按 cycleRate_ 匀速绕环；否则向目标锚点以 blendRate_ 收敛。
    void AdvanceTime(float dt) noexcept
    {
        const auto n = static_cast<float>(Sample::Showcase::CyberCityTimePresets().size());
        if (n <= 0.0f)
            return;
        if (autoCycle_)
        {
            dayTime_ += dt * cycleRate_;
            targetDayTime_ = dayTime_;
        }
        else
        {
            float diff = targetDayTime_ - dayTime_;
            if (diff > n * 0.5f)
                diff -= n;
            if (diff < -n * 0.5f)
                diff += n;
            const float step = dt * blendRate_;
            if (std::fabs(diff) <= step)
                dayTime_ = targetDayTime_;
            else
                dayTime_ += std::copysign(step, diff);
        }
        // 归一到 [0, n)
        dayTime_ = std::fmod(dayTime_, n);
        if (dayTime_ < 0.0f)
            dayTime_ += n;
        targetDayTime_ = std::fmod(targetDayTime_, n);
        if (targetDayTime_ < 0.0f)
            targetDayTime_ += n;
    }

    // 当前实际生效的时段（相邻两预设按小数位插值），HUD 与光照落地都读它
    [[nodiscard]] Sample::Showcase::CyberTimePreset BlendedPreset() const
    {
        const std::vector<Sample::Showcase::CyberTimePreset>& ps = Sample::Showcase::CyberCityTimePresets();
        if (ps.empty())
            return Sample::Showcase::CyberTimePreset{};
        const auto n = static_cast<int>(ps.size());
        const int i0 = static_cast<int>(std::floor(dayTime_)) % n;
        const float frac = dayTime_ - std::floor(dayTime_);
        const int i1 = (i0 + 1) % n;
        return Sample::Showcase::BlendTimePresets(ps[static_cast<std::size_t>(i0)], ps[static_cast<std::size_t>(i1)],
                                                  frac);
    }

    // 目标锚点预设（HUD 显示"正在切到 xx"用；非过渡态与 BlendedPreset 一致）
    [[nodiscard]] const Sample::Showcase::CyberTimePreset& Preset() const
    {
        const std::vector<Sample::Showcase::CyberTimePreset>& ps = Sample::Showcase::CyberCityTimePresets();
        const std::size_t i =
            static_cast<std::size_t>(std::clamp(TimeOfDayIndex(), 0, static_cast<int>(ps.size()) - 1));
        return ps[i];
    }

    [[nodiscard]] bool AutoCycle() const noexcept { return autoCycle_; }
    void SetAutoCycle(bool v) noexcept { autoCycle_ = v; }
    void ToggleAutoCycle() noexcept { autoCycle_ = !autoCycle_; }
    [[nodiscard]] float DayTime() const noexcept { return dayTime_; }

    // ---- 画面风格（G 键循环） ----
    void CycleLook() noexcept
    {
        const auto n = static_cast<int>(Sample::Showcase::CyberCityLookPresets().size());
        if (n > 0)
            lookIndex_ = (lookIndex_ + 1) % n;
    }

    void SetLook(int index) noexcept
    {
        const auto n = static_cast<int>(Sample::Showcase::CyberCityLookPresets().size());
        if (n > 0)
            lookIndex_ = ((index % n) + n) % n;
    }

    [[nodiscard]] int LookIndex() const noexcept { return lookIndex_; }

    [[nodiscard]] const Sample::Showcase::CyberLookPreset& Look() const
    {
        const std::vector<Sample::Showcase::CyberLookPreset>& ls = Sample::Showcase::CyberCityLookPresets();
        const std::size_t i = static_cast<std::size_t>(std::clamp(lookIndex_, 0, static_cast<int>(ls.size()) - 1));
        return ls[i];
    }

    // ---- 已体验特性打点（HUD 进度） ----
    void MarkFeature(int featureId) noexcept
    {
        if (featureId >= 1 && featureId <= 31)
            visitedMask_ |= (1u << static_cast<uint32_t>(featureId));
    }

    [[nodiscard]] bool Visited(int featureId) const noexcept
    {
        if (featureId < 1 || featureId > 31)
            return false;
        return (visitedMask_ & (1u << static_cast<uint32_t>(featureId))) != 0u;
    }

    [[nodiscard]] int VisitedCount() const noexcept
    {
        int n = 0;
        for (int i = 1; i <= 31; ++i)
            if (Visited(i))
                ++n;
        return n;
    }

  private:
    std::vector<Sample::Showcase::CyberExhibit> exhibits_;
    glm::vec3 spawn_{0.0f, 0.0f, 18.0f};
    float spawnYaw_ = 3.14159265f;
    bool active_ = false;
    int focusIndex_ = -1;
    float focusDistance_ = 0.0f;
    uint32_t visitedMask_ = 0;

    // 连续昼夜状态：dayTime_ 为环上位置 [0, n)，targetDayTime_ 为要收敛到的锚点
    static constexpr float kDefaultDayTime = 1.0f; // 默认夜晚（赛博朋克主视觉）
    float dayTime_ = kDefaultDayTime;
    float targetDayTime_ = kDefaultDayTime;
    bool autoCycle_ = false;
    float cycleRate_ = 1.0f / 18.0f; // 自动循环：18 秒推进一个预设（整圈 ~72 秒）
    float blendRate_ = 1.0f / 1.6f;  // 手动切换：1.6 秒完成一次锚点过渡
    int lookIndex_ = 0;              // 当前画面风格（CyberCityLookPresets 索引）
};
} // namespace BigHero
