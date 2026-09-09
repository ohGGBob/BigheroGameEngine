#pragma once
#include <cstdint>
#include <deque>

namespace bighero {

// FrameTiming: measures and reports frame time statistics (avg, min, max, fps)
// over a sliding window. Self-contained, std-lib only.
class FrameTiming {
public:
    explicit FrameTiming(size_t window = 60) : window_(window ? window : 60) {}

    void Begin() { frameStart_ = Now(); }
    // End the frame and record its duration.
    void End() {
        double dt = Now() - frameStart_;
        Record(dt);
    }
    void Record(double dt) {
        if (dt < 0) dt = 0;
        samples_.push_back(dt);
        sum_ += dt;
        if (samples_.size() > window_) { sum_ -= samples_.front(); samples_.pop_front(); }
        if (dt > max_) max_ = dt;
        if (dt < min_ || min_ <= 0) min_ = dt;
    }

    double Average() const { return samples_.empty() ? 0 : sum_ / samples_.size(); }
    double Min() const { return min_; }
    double Max() const { return max_; }
    double Fps() const { double a = Average(); return a > 0 ? 1.0 / a : 0; }
    size_t SampleCount() const { return samples_.size(); }
    void Reset() { samples_.clear(); sum_ = 0; min_ = max_ = 0; }

    static double Now() {
        using namespace std::chrono;
        return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
    }

private:
    std::deque<double> samples_;
    double sum_ = 0, min_ = 0, max_ = 0;
    double frameStart_ = 0;
    size_t window_ = 60;
};

} // namespace bighero
