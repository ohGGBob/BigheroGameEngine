#pragma once
#include <cstdint>
#include <functional>

namespace bighero {

// Task: a lightweight unit of work with id, priority and state. Can be chained
// with a completion callback. Self-contained, std-lib only.
class Task {
public:
    enum class State : int { Idle = 0, Submitted = 1, Running = 2, Completed = 3, Failed = 4, Canceled = 5 };

    Task() = default;
    explicit Task(std::function<void()> fn, int priority = 0) : priority_(priority), fn_(std::move(fn)) {}

    void SetId(uint64_t id) { id_ = id; }
    uint64_t Id() const { return id_; }
    void SetPriority(int p) { priority_ = p; }
    int Priority() const { return priority_; }

    void SetFunction(std::function<void()> fn) { fn_ = std::move(fn); }
    bool HasFunction() const { return (bool)fn_; }

    // Run the task body; sets state and invokes callback.
    void Execute() {
        state_ = State::Running;
        if (fn_) fn_();
        state_ = State::Completed;
        if (onComplete_) onComplete_();
    }
    // Run and mark failed if an exception is thrown.
    void ExecuteSafe() {
        state_ = State::Running;
        try {
            if (fn_) fn_();
            state_ = State::Completed;
        } catch (...) {
            state_ = State::Failed;
        }
        if (onComplete_) onComplete_();
    }

    void SetCompletion(std::function<void()> cb) { onComplete_ = std::move(cb); }
    State GetState() const { return state_; }
    bool IsDone() const { return state_ == State::Completed || state_ == State::Failed || state_ == State::Canceled; }
    void Cancel() { state_ = State::Canceled; }

    static const char* StateName(State s) {
        switch (s) {
            case State::Idle: return "Idle";
            case State::Submitted: return "Submitted";
            case State::Running: return "Running";
            case State::Completed: return "Completed";
            case State::Failed: return "Failed";
            case State::Canceled: return "Canceled";
        }
        return "Unknown";
    }

private:
    uint64_t id_ = 0;
    int priority_ = 0;
    State state_ = State::Idle;
    std::function<void()> fn_;
    std::function<void()> onComplete_;
};

} // namespace bighero
