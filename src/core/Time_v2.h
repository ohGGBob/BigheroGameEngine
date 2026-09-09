#pragma once
#include <cstdint>
#include <chrono>

namespace bighero {

// Time: simple high-resolution wall-clock helpers. Header-only, uses
// std::chrono. Self-contained, std-lib only.
class Time {
public:
    Time() = default;

    static double Now() {
        using namespace std::chrono;
        return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
    }
    static double NowMilliseconds() { return Now() * 1000.0; }

    void Reset() { start_ = Now(); }
    double Elapsed() const { return Now() - start_; }
    double ElapsedMilliseconds() const { return (Now() - start_) * 1000.0; }

    int AccumulateFixedStep(double dt, double fixedStep, double& accumulator) const {
        accumulator += dt;
        int steps = 0;
        while (accumulator >= fixedStep && fixedStep > 0) {
            accumulator -= fixedStep;
            ++steps;
        }
        return steps;
    }

    static double FpsFromDt(double dt) { return dt > 0 ? 1.0 / dt : 0.0; }

private:
    double start_ = 0;
};

} // namespace bighero
