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
// - BuildSummary() 把当前帧各作用域按键聚合，输出 count/total/avg/max 并按 max 降序，
//   用于定位 CPU 热点（哪些系统最耗时），是商业化性能面板的常用能力。
// - 纯头文件、零动态分配（records_ 容量随场景稳定后不再增长）

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <string_view>
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

    // 聚合后的作用域统计：同一 name 下 count 次采样的累计/平均/最大耗时。
    struct ScopeSummary
    {
        const char* name;
        size_t count;
        float totalMs;
        float avgMs;
        float maxMs;
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

    // 把当前帧各作用域（records_）按键聚合为统计结果，并按 maxMs 降序排序。
    // 用途：商业化性能面板中定位 CPU 热点（最耗时的系统排在最前）。
    // 返回的结果会按 name 首现顺序分组；maxResults=0 表示返回全量。
    [[nodiscard]] std::vector<ScopeSummary> BuildSummary(size_t maxResults = 0) const
    {
        std::vector<ScopeSummary> out;
        if (records_.empty())
            return out;

        // 维护每个 name 在 out 中的索引，O(n) 聚合。
        std::vector<size_t> indexOfName;
        std::vector<const char*> names;

        for (const ScopeRecord& r : records_)
        {
            size_t idx = names.size();
            bool found = false;
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (std::string_view(names[i]) == std::string_view(r.name))
                {
                    idx = i;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                names.push_back(r.name);
                out.push_back({r.name, 0, 0.0f, 0.0f, 0.0f});
            }
            ScopeSummary& s = out[idx];
            s.count += 1;
            s.totalMs += r.ms;
            if (r.ms > s.maxMs)
                s.maxMs = r.ms;
        }

        for (ScopeSummary& s : out)
            s.avgMs = s.count > 0 ? s.totalMs / static_cast<float>(s.count) : 0.0f;

        // 按 maxMs 降序（热点优先），耗时相同则保持稳定（相同时按 name 稳定性由算法保证）。
        std::stable_sort(out.begin(), out.end(),
                         [](const ScopeSummary& a, const ScopeSummary& b) { return a.maxMs > b.maxMs; });

        if (maxResults > 0 && out.size() > maxResults)
            out.resize(maxResults);
        return out;
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
