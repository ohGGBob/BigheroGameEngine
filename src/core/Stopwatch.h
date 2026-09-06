#pragma once
// 高精度计时器（Stopwatch）：纯标准库、仅头文件。
// 用于性能测量、帧时间统计、系统耗时采样（商业引擎 Profiling 面板的基础）。
//
// 提供：
//   - Start()/Stop()/Reset()：逐个案例计时，Elapsed() 返回秒。
//   - Lap()：单次测量（一次 Start/Lap 组合即完成一次采样）。
//   - 基于 steady_clock（单调），不受系统时钟调整影响。

#include <chrono>

namespace BigHero::Core
{
class Stopwatch
{
  public:
    using Clock = std::chrono::steady_clock;

    void Start() noexcept
    {
        start_ = Clock::now();
        running_ = true;
    }
    void Stop() noexcept
    {
        if (running_)
        {
            last_ = Clock::now() - start_;
            running_ = false;
        }
    }
    void Reset() noexcept
    {
        last_ = Clock::duration::zero();
        running_ = false;
    }

    // 已计时的秒数（未在运行时为上次 Stop 的累积；运行中为当前读数）。
    [[nodiscard]] double ElapsedSeconds() const noexcept
    {
        if (running_)
            return DurationSeconds(Clock::now() - start_);
        return DurationSeconds(last_);
    }
    [[nodiscard]] double ElapsedMilliseconds() const noexcept { return ElapsedSeconds() * 1000.0; }

    // 单次采样：Start + Lap == 一次测量（等价 Start 后立即读）。
    [[nodiscard]] double Lap() noexcept
    {
        Start();
        return ElapsedMilliseconds();
    }

    [[nodiscard]] bool IsRunning() const noexcept { return running_; }

  private:
    static double DurationSeconds(Clock::duration d) noexcept
    {
        return std::chrono::duration<double>(d).count();
    }

    Clock::time_point start_{};
    Clock::duration last_ = Clock::duration::zero();
    bool running_ = false;
};
} // namespace BigHero::Core
