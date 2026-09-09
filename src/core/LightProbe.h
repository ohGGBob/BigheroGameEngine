#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bighero {

// LightProbe: captures indirect/ambient light at a point in space. Holds a
// set of intensity samples (one per direction) and provides an interpolated
// lookup by direction. Pure CPU-side data + helper.
class LightProbe {
public:
    struct Sample { float dirX, dirY, dirZ; float r, g, b; };

    LightProbe() {}
    explicit LightProbe(std::uint64_t id) : id_(id) {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }
    void SetId(std::uint64_t id) { id_ = id; }
    std::uint64_t Id() const { return id_; }

    void AddSample(float dx, float dy, float dz, float r, float g, float b) {
        float len = std::sqrt(dx*dx+dy*dy+dz*dz);
        if (len > 0) { dx/=len; dy/=len; dz/=len; }
        samples_.push_back({dx, dy, dz, r, g, b});
    }
    std::size_t SampleCount() const { return samples_.size(); }
    bool GetSample(std::size_t i, Sample& out) const {
        if (i >= samples_.size()) return false;
        out = samples_[i]; return true;
    }

    // Intensity lookup: returns the sample closest to a direction.
    void SampleDirection(float dx, float dy, float dz, float& r, float& g, float& b) const {
        r = 0; g = 0; b = 0;
        if (samples_.empty()) return;
        float len = std::sqrt(dx*dx+dy*dy+dz*dz);
        if (len > 0) { dx/=len; dy/=len; dz/=len; }
        float bestDot = -2.0f;
        const Sample* best = &samples_[0];
        for (auto& s : samples_) {
            float dot = s.dirX*dx + s.dirY*dy + s.dirZ*dz;
            if (dot > bestDot) { bestDot = dot; best = &s; }
        }
        r = best->r; g = best->g; b = best->b;
    }

    void Clear() { samples_.clear(); }

private:
    std::uint64_t id_ = 0;
    float px_=0, py_=0, pz_=0;
    std::vector<Sample> samples_;
};

} // namespace bighero
