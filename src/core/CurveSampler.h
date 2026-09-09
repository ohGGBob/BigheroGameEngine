#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// CurveSampler: reads samples from a pre-baked monotonic curve table using
// linear interpolation. Holds a value array plus optional time step, has
// helper Evaluate / Zero / sampling. Pure CPU-side accessor.
class CurveSampler {
public:
    CurveSampler() {}
    explicit CurveSampler(const float* data, std::size_t count) {
        samples_.assign(data, data + count);
    }

    void SetSamples(const float* data, std::size_t count) {
        samples_.assign(data, data + count);
    }
    void SetTimeStep(float dt) { timeStep_ = dt <= 0 ? 1.0f : dt; }
    float TimeStep() const { return timeStep_; }
    std::size_t Count() const { return samples_.size(); }
    float Sample(std::size_t i) const { return i < samples_.size() ? samples_[i] : 0.0f; }

    // Evaluate at normalized t in [0,1] with linear interpolation.
    float Evaluate(float t) const {
        if (samples_.empty()) return 0.0f;
        if (samples_.size() == 1) return samples_[0];
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float pos = t * (float)(samples_.size() - 1);
        std::size_t i = (std::size_t)pos;
        if (i >= samples_.size() - 1) return samples_.back();
        float f = pos - (float)i;
        return samples_[i] + (samples_[i+1] - samples_[i]) * f;
    }

    float Max() const {
        if (samples_.empty()) return 0.0f;
        float m = samples_[0];
        for (auto v : samples_) if (v > m) m = v;
        return m;
    }
    float Min() const {
        if (samples_.empty()) return 0.0f;
        float m = samples_[0];
        for (auto v : samples_) if (v < m) m = v;
        return m;
    }
    void Zero() { for (auto& v : samples_) v = 0.0f; }

private:
    std::vector<float> samples_;
    float timeStep_ = 1.0f;
};

} // namespace bighero
