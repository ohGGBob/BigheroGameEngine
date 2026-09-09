#pragma once
#include <cstdint>
#include <mutex>
#include <condition_variable>

namespace bighero {

// CondVar: a thin RAII-friendly wrapper around std::condition_variable with a
// predicate-based wait. Self-contained, std-lib only.
class CondVar {
public:
    CondVar() = default;

    // Wait until predicate returns true.
    template <class Pred>
    void Wait(std::unique_lock<std::mutex>& lock, Pred pred) {
        cv_.wait(lock, pred);
    }
    // Wait for a predicate or timeout; returns true if predicate satisfied.
    template <class Pred>
    bool WaitFor(std::unique_lock<std::mutex>& lock, int timeoutMs, Pred pred) {
        return cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred);
    }
    void NotifyOne() { cv_.notify_one(); }
    void NotifyAll() { cv_.notify_all(); }

private:
    std::condition_variable cv_;
};

} // namespace bighero
