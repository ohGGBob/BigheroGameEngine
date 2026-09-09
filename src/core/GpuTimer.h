#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace bighero {

// GpuTimer: measures GPU execution time of a section (frame, pass, or draw).
// The backend records timestamp queries and later copies the elapsed time.
// Pure CPU-side descriptor + result holder.
class GpuTimer {
public:
    enum class State { Idle, Recording, Ready };

    GpuTimer() {}
    explicit GpuTimer(const char* name) : name_(name ? name : "") {}

    void SetName(const char* n) { name_ = n ? n : ""; }
    const char* Name() const { return name_.c_str(); }

    void Begin() { state_ = State::Recording; }
    void End() { state_ = State::Recording; }
    bool IsRecording() const { return state_ == State::Recording; }

    // Backend sets elapsed microseconds.
    void SetElapsedMicros(double us, const char* label = nullptr) {
        elapsedUs_ = us;
        if (label) name_ = label;
        state_ = State::Ready;
    }
    bool IsReady() const { return state_ == State::Ready; }
    double ElapsedMicros() const { return elapsedUs_; }
    double ElapsedMillis() const { return elapsedUs_ / 1000.0; }

    void Reset() { state_ = State::Idle; elapsedUs_ = 0.0; }

private:
    std::string name_;
    State state_ = State::Idle;
    double elapsedUs_ = 0.0;
};

} // namespace bighero
