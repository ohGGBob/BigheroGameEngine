#pragma once
#include <cstdint>
#include <chrono>

namespace bighero {

// Stopwatch: measures real elapsed time across named phases. Uses std::chrono
// for wall-clock resolution. Header-only, std-lib only.
class Stopwatch {
public:
    Stopwatch() = default;

    void Start() {
        startNs_ = NowNs();
        running_ = true;
    }
    void Stop() {
        if (running_) {
            accumulatedNs_ += NowNs() - startNs_;
            running_ = false;
        }
    }
    void Reset() { running_ = false; startNs_ = 0; accumulatedNs_ = 0; lastLapNs_ = 0; }

    bool IsRunning() const { return running_; }
    double ElapsedSeconds() const { return ElapsedMilliseconds() / 1000.0; }
    double ElapsedMilliseconds() const {
        int64_t total = accumulatedNs_;
        if (running_) total += NowNs() - startNs_;
        return (double)total / 1e6;
    }
    double ElapsedMicroseconds() const {
        int64_t total = accumulatedNs_;
        if (running_) total += NowNs() - startNs_;
        return (double)total / 1e3;
    }

    double LapMilliseconds() {
        int64_t now = NowNs();
        int64_t lap = now - lastLapNs_;
        lastLapNs_ = now;
        return (double)lap / 1e6;
    }

private:
    static int64_t NowNs() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }
    bool running_ = false;
    int64_t startNs_ = 0;
    int64_t accumulatedNs_ = 0;
    int64_t lastLapNs_ = 0;
};

} // namespace bighero
