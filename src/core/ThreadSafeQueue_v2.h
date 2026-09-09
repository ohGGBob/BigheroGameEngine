#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <condition_variable>

namespace bighero {

// ThreadSafeQueue: a mutex-protected FIFO queue with blocking push/pop and a
// size cap. Self-contained, std-lib only.
template <class T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t capacity = 0) : capacity_(capacity) {}

    void Push(const T& v) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (capacity_ && queue_.size() >= capacity_) return; // drop when full
            queue_.push_back(v);
        }
        cvNotEmpty_.notify_one();
    }
    void Push(T&& v) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (capacity_ && queue_.size() >= capacity_) return;
            queue_.push_back(std::move(v));
        }
        cvNotEmpty_.notify_one();
    }

    // Blocking pop: waits until an item is available.
    T Pop() {
        std::unique_lock<std::mutex> lk(mutex_);
        cvNotEmpty_.wait(lk, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty()) return T();
        T v = std::move(queue_.front());
        queue_.pop_front();
        return v;
    }
    bool TryPop(T& out) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_.empty();
    }
    size_t Size() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_.size();
    }
    void Clear() {
        std::lock_guard<std::mutex> lk(mutex_);
        queue_.clear();
    }
    void Close() {
        std::lock_guard<std::mutex> lk(mutex_);
        closed_ = true;
        cvNotEmpty_.notify_all();
    }
    bool IsClosed() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return closed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cvNotEmpty_;
    std::deque<T> queue_;
    size_t capacity_ = 0;
    bool closed_ = false;
};

} // namespace bighero
