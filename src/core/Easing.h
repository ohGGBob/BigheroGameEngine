#pragma once
// 缓动函数库（Easing）：常用插值缓动曲线。
// 纯标准库、仅头文件。
//
// 商业化价值：UI 过渡动画、摄像机运动、粒子爆发生命周期、物理过冲等标准缓动，
// 提供平滑的非线性插值；避免每个模块手写曲线导致不一致。
//
// 约定：输入 t 通常为 [0,1]（归一化时间），返回 [0,1]（可过冲，如 Back/Elastic）。
//   Linear / EaseInQuad / EaseOutQuad / EaseInOutQuad / EaseInCubic / EaseOutCubic /
//   EaseInOutCubic / EaseOutBack / EaseOutElastic / SmoothStep(近似 easeInOut)。

#include <algorithm>
#include <cmath>

namespace BigHero::Core
{
namespace Easing
{
    inline float Linear(float t) { return t; }

    inline float InQuad(float t) { return t * t; }
    inline float OutQuad(float t) { return t * (2.0f - t); }
    inline float InOutQuad(float t)
    {
        t *= 2.0f;
        if (t < 1.0f) return 0.5f * t * t;
        --t;
        return 0.5f * (1.0f - t * t); // -0.5*(t*(t-2)) 的等价形式
    }

    inline float InCubic(float t) { return t * t * t; }
    inline float OutCubic(float t) { t -= 1.0f; return t * t * t + 1.0f; }
    inline float InOutCubic(float t)
    {
        t *= 2.0f;
        if (t < 1.0f) return 0.5f * t * t * t;
        t -= 2.0f;
        return 0.5f * (t * t * t + 2.0f);
    }

    inline float OutBack(float t)
    {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        t -= 1.0f;
        return 1.0f + c3 * t * t * t + c1 * t * t;
    }

    inline float OutElastic(float t)
    {
        constexpr float c4 = (2.0f * 3.14159265f) / 3.0f;
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }

    inline float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }
} // namespace Easing
} // namespace BigHero::Core
