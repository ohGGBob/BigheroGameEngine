#pragma once
// 轻量级 CPU 帧时间分段剖析器。
//
// 用法：
//   FrameProfiler profiler;
//   profiler.BeginFrame();
//   {
//       FrameProfiler::Scope s(profiler, "UpdateCamera");
//       // ... 代码 ...
//   } // 析构时自动记录耗时
//   profiler.EndFrame();
//
// 设计要点：
// - Scope 为 RAII，进入作用域计时、离开作用域记录，零遗忘风险
// - 每帧记录存于 vector，EndFrame 后可读取；BeginFrame 清空复用
// - 帧率历史存于环形缓冲（kHistorySize 帧），供编辑器绘制帧率曲线
// - 纯头文件、零动态分配（records_ 容量随场景稳定后不再增长）

#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

namespace BigHero::Core
{
class FrameProfiler
{
  public:
    struct ScopeRecord
    {
        const char* name;
        float ms;
    };

    static constexpr size_t kHistorySize = 180; // 约 3 秒（60fps）

    // RAII 作用域计时器：构造时取时间戳，析构时计算耗时并写入 profiler
    class Scope
    {
      public:
        Scope(FrameProfiler& profiler, const char* name) : profiler_(profiler), name_(name), start_(Clock::now()) {}
        ~Scope()
        {
            const float ms = std::chrono::duration<float, std::milli>(Clock::now() - start_).count();
            profiler_.Record(name_, ms);
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

      private:
        using Clock = std::chrono::high_resolution_clock;
        FrameProfiler& profiler_;
        const char* name_;
        Clock::time_point start_;
    };

    void BeginFrame()
    {
        records_.clear();
        frameStart_ = Clock::now();
    }

    void EndFrame()
    {
        totalMs_ = std::chrono::duration<float, std::milli>(Clock::now() - frameStart_).count();
        history_[historyIndex_] = totalMs_;
        historyIndex_ = (historyIndex_ + 1) % kHistorySize;
        if (historyCount_ < kHistorySize)
            ++historyCount_;
    }

    void Record(const char* name, float ms) { records_.push_back({name, ms}); }

    [[nodiscard]] const std::vector<ScopeRecord>& Records() const noexcept { return records_; }
    [[nodiscard]] float TotalMs() const noexcept { return totalMs_; }
    [[nodiscard]] float Fps() const noexcept { return totalMs_ > 0.0f ? 1000.0f / totalMs_ : 0.0f; }

    // 帧率历史：从最旧到最新排列的有效数据（供 ImGui::PlotLines 直接使用）
    [[nodiscard]] const std::array<float, kHistorySize>& History() const noexcept { return history_; }
    [[nodiscard]] size_t HistoryIndex() const noexcept { return historyIndex_; }
    [[nodiscard]] size_t HistoryCount() const noexcept { return historyCount_; }

    // 将环形缓冲中的历史按时间顺序（最旧→最新）写入 out，返回实际写入数量
    size_t GetHistoryChronological(float* out, size_t maxCount) const noexcept
    {
        const size_t n = std::min(maxCount, historyCount_);
        for (size_t i = 0; i < n; ++i)
            out[i] = history_[(historyIndex_ + i) % kHistorySize];
        return n;
    }

  private:
    using Clock = std::chrono::high_resolution_clock;

    std::vector<ScopeRecord> records_;
    float totalMs_ = 0.0f;
    Clock::time_point frameStart_{};
    std::array<float, kHistorySize> history_{};
    size_t historyIndex_ = 0;
    size_t historyCount_ = 0;
};
} // namespace BigHero::Core
