#pragma once
// 补间动画值（Tween）：驱动一个数值/颜色从起点到终点按缓动曲线过渡。
// 纯标准库、仅头文件。
//
// 商业化价值：UI 过渡、属性动画、摄像机运镜、数值插值的高层封装；
// 配合 Easing 曲线，用简单的 Start/Update 驱动一个值随时间变化。
//
// 设计：
//   - 持有 from/to 数值、时长、缓动函数、是否循环、是否完成。
//   - Start(from,to,duration,easing)：重置并开始。
//   - Update(dt)：推进时间，返回当前插值（并标记完成）。
//   - IsFinished/ReachedEnd 查询。

#include "Easing.h"
#include <functional>

namespace BigHero::Core
{
class Tween
{
  public:
    // 缓动函数类型：输入 t[0,1] 输出进度[0,1]（可过冲）。默认线性。
    using EasingFn = std::function<float(float)>;

    Tween() { SetEasing(Easing::Linear); }

    Tween& Start(float from, float to, float durationSeconds, bool loop = false)
    {
        from_ = from;
        to_ = to;
        duration_ = durationSeconds > 0.0f ? durationSeconds : 0.0001f;
        loop_ = loop;
        time_ = 0.0f;
        finished_ = false;
        return *this;
    }

    // 更新 dt，返回当前插值。
    float Update(float dt)
    {
        if (finished_)
            return CurrentValue();
        time_ += dt;
        float t = time_ / duration_;
        if (t >= 1.0f)
        {
            if (loop_)
            {
                t = fmodf(t, 1.0f);
                time_ = t * duration_;
            }
            else
            {
                t = 1.0f;
                finished_ = true;
            }
        }
        return from_ + (to_ - from_) * easing_(t);
    }

    [[nodiscard]] float CurrentValue() const
    {
        float t = (duration_ > 0.0f) ? (time_ / duration_) : 1.0f;
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;
        return from_ + (to_ - from_) * easing_(t);
    }

    void SetEasing(EasingFn fn) { easing_ = fn ? std::move(fn) : Easing::Linear; }
    void Reset() { time_ = 0.0f; finished_ = false; }
    void Stop() { finished_ = true; }

    [[nodiscard]] bool IsFinished() const { return finished_; }
    [[nodiscard]] float Progress() const
    {
        return duration_ > 0.0f ? (time_ / duration_) : (finished_ ? 1.0f : 0.0f);
    }
    [[nodiscard]] float From() const { return from_; }
    [[nodiscard]] float To() const { return to_; }

  private:
    float from_ = 0, to_ = 0;
    float duration_ = 1.0f;
    float time_ = 0.0f;
    bool loop_ = false;
    bool finished_ = false;
    EasingFn easing_ = Easing::Linear;
};
} // namespace BigHero::Core
