#pragma once
// 帧剖析器（Profiler）：带命名区间的 CPU 耗时统计（RAII）。
// 纯标准库、仅头文件。
//
// 商业化价值：性能定位的基础——标记系统/子系统 CPU 耗时，输出平均/峰值/总时长；
// 是"慢在哪"量化分析的标准手段。

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace BigHero::Core
{
class Profiler
{
  public:
    struct Sample
    {
        double totalMs = 0;   // 累加
        double maxMs = 0;     // 最长单次
        double minMs = 0;     // 最短单次
        uint64_t count = 0;   // 调用次数
    };

    void Begin(const char* name)
    {
        auto start = Clock::now();
        std::lock_guard<std::mutex> lock(mx_);
        activeStarts_[name] = start;
    }

    void End(const char* name)
    {
        auto end = Clock::now();
        std::lock_guard<std::mutex> lock(mx_);
        auto it = activeStarts_.find(name);
        if (it == activeStarts_.end())
            return;
        double ms = std::chrono::duration<double, std::milli>(end - it->second).count();
        activeStarts_.erase(it);
        Sample& s = samples_[name];
        s.totalMs += ms;
        s.count++;
        if (s.count == 1 || ms < s.minMs) s.minMs = ms;
        if (ms > s.maxMs) s.maxMs = ms;
    }

    // 读取名为 name 的样本统计（不存在返回 nullptr）。
    const Sample* Query(const char* name) const
    {
        std::lock_guard<std::mutex> lock(mx_);
        auto it = samples_.find(name);
        return it == samples_.end() ? nullptr : &it->second;
    }

    // 复制全部样本统计（便于无锁读取）。
    std::unordered_map<std::string, Sample> Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mx_);
        return samples_;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(mx_);
        samples_.clear();
        activeStarts_.clear();
    }

  private:
    using Clock = std::chrono::steady_clock;
    std::unordered_map<std::string, Sample> samples_;
    std::unordered_map<std::string, Clock::time_point> activeStarts_;
    mutable std::mutex mx_;
};

// RAII 作用域剖析：进入记 Begin，退出记 End。
class ScopedProfiler
{
  public:
    ScopedProfiler(Profiler& prof, const char* name) : prof_(prof), name_(name)
    {
        prof_.Begin(name_);
    }
    ~ScopedProfiler() { prof_.End(name_); }
    ScopedProfiler(const ScopedProfiler&) = delete;
    ScopedProfiler& operator=(const ScopedProfiler&) = delete;

  private:
    Profiler& prof_;
    const char* name_;
};
} // namespace BigHero::Core
