#pragma once
#include <cstdint>

namespace bighero {

// Fence: a GPU-CPU synchronization primitive used to signal completion of
// submitted work. Self-contained, std-lib only.
class Fence {
public:
    enum class State : uint8_t { Unsigned = 0, Signaled = 1 };

    Fence() = default;
    explicit Fence(bool initiallySignaled)
        : state_(initiallySignaled ? State::Signaled : State::Unsigned) {}

    void Reset() { state_ = State::Unsigned; }
    void Signal() { state_ = State::Signaled; }
    void Wait() { /* no-op in this CPU model; real harness consumes signal */ }
    bool IsSignaled() const { return state_ == State::Signaled; }
    bool IsUnsigned() const { return state_ == State::Unsigned; }

    void SetState(State s) { state_ = s; }
    State GetState() const { return state_; }

    void SetTimeout(uint64_t nanos) { timeoutNs_ = nanos; }
    uint64_t Timeout() const { return timeoutNs_; }

private:
    State state_ = State::Unsigned;
    uint64_t timeoutNs_ = 1000000000ULL; // 1s
};

} // namespace bighero
