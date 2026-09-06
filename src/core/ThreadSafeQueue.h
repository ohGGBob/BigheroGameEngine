#pragma once
// 线程安全队列（ThreadSafeQueue<T>）：双端队列 + 互斥锁 + 条件变量，blocking 与 non-blocking API。
// 纯标准库、仅头文件。
//
// 商业化价值：作业系统（job system）、渲染/资源加载任务队列、网络/音频输入消息队列的
// 生产者-消费者通信原语。
//
// 提供：
//   - PushBack / PushFront：入队（生产者）。
//   - TryPopFront / TryPopBack：非阻塞出队（消费者，成功返回 true）。
//   - WaitPopFront：阻塞直到有元素可弹出（消费者等待）。
//   - Size / Empty / Clear。容量可选（用 Capacity 字段，push 满时返回 false 不阻塞）。

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <deque>

namespace BigHero::Core
{
template<typename T> class ThreadSafeQueue
{
  public:
    explicit ThreadSafeQueue(size_t maxCapacity = 0) : maxCapacity_(maxCapacity) {}

    // 入队（尾部）。超过 maxCapacity（若>0）时返回 false 不阻塞。
    bool PushBack(const T& v)
    {
        std::lock_guard<std::mutex> lock(mx_);
        if (maxCapacity_ > 0 && q_.size() >= maxCapacity_)
            return false;
        q_.push_back(v);
        cv_.notify_one();
        return true;
    }
    bool PushBack(T&& v)
    {
        std::lock_guard<std::mutex> lock(mx_);
        if (maxCapacity_ > 0 && q_.size() >= maxCapacity_)
            return false;
        q_.push_back(std::move(v));
        cv_.notify_one();
        return true;
    }
    // 入队（头部），优先级高的任务插队。
    bool PushFront(T v)
    {
        std::lock_guard<std::mutex> lock(mx_);
        if (maxCapacity_ > 0 && q_.size() >= maxCapacity_)
            return false;
        q_.push_front(std::move(v));
        cv_.notify_one();
        return true;
    }

    // 非阻塞出队（头部）。成功返回 true 并把值写到 out。
    bool TryPopFront(T& out)
    {
        std::lock_guard<std::mutex> lock(mx_);
        if (q_.empty())
            return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    bool TryPopBack(T& out)
    {
        std::lock_guard<std::mutex> lock(mx_);
        if (q_.empty())
            return false;
        out = std::move(q_.back());
        q_.pop_back();
        return true;
    }

    // 阻塞出队（头部），直到有元素（或被 notify）。
    void WaitPopFront(T& out)
    {
        std::unique_lock<std::mutex> lock(mx_);
        cv_.wait(lock, [this] { return !q_.empty(); });
        out = std::move(q_.front());
        q_.pop_front();
    }

    [[nodiscard]] size_t Size()
    {
        std::lock_guard<std::mutex> lock(mx_);
        return q_.size();
    }
    [[nodiscard]] bool Empty()
    {
        std::lock_guard<std::mutex> lock(mx_);
        return q_.empty();
    }
    void Clear()
    {
        std::lock_guard<std::mutex> lock(mx_);
        q_.clear();
    }
    void SetCapacity(size_t max)
    {
        std::lock_guard<std::mutex> lock(mx_);
        maxCapacity_ = max;
    }

  private:
    std::deque<T> q_;
    mutable std::mutex mx_;
    std::condition_variable cv_;
    size_t maxCapacity_ = 0;
};
} // namespace BigHero::Core
