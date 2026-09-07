#pragma once
// 作用域计时器（ScopedTimer）：RAII 计时，析构时输出耗时。
// 纯标准库、仅头文件。
//
// 商业化价值：性能剖析、帧耗时追踪、自动日志埋点的轻量手段；
// 用 RAII 保证作用域退出必然记录耗时，无需手动 stop。

#include <chrono>
#include <cstdio>
#include <string_view>

namespace BigHero::Core
{
class ScopedTimer
{
  public:
    explicit ScopedTimer(std::string_view label, bool logOnDestruct = true)
        : label_(label), logOnDestruct_(logOnDestruct), start_(Clock::now()) {}

    double ElapsedMillis() const
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }
    void Restart() { start_ = Clock::now(); }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    ~ScopedTimer()
    {
        if (logOnDestruct_)
            std::printf("[ScopedTimer] %.*s took %.3f ms\n", (int)label_.size(),
                        label_.data(), ElapsedMillis());
    }

  private:
    using Clock = std::chrono::steady_clock;
    std::string_view label_;
    bool logOnDestruct_;
    Clock::time_point start_;
};
} // namespace BigHero::Core
