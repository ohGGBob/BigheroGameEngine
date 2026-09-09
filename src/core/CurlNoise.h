#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// CurlNoise: a divergence-free 2D vector field derived from the curl of a
// scalar noise potential, used for advecting particles/flow without sinks.
// Standard-library only, self-contained.
class CurlNoise {
public:
    CurlNoise(float scale = 1.0f) : scale_(scale) {}
    void SetScale(float s) { scale_ = s < 1e-6f ? 1e-6f : s; }
    float Scale() const { return scale_; }

    // Sample the curl noise vector field at (x,y); returns (vx,vy).
    template <typename NoiseFn>
    void Sample(NoiseFn& noise, float x, float y, float& vx, float& vy) const {
        const float e = 0.01f * scale_;
        // Potential field is the noise scalar.
        float p0 = noise.Noise(x, y);
        float px = noise.Noise(x + e, y);
        float py = noise.Noise(x, y + e);
        // Curl of (psi) in 2D: (dpsi/dy, -dpsi/dx).
        vx = (py - p0) / e;
        vy = -(px - p0) / e;
    }

private:
    float scale_;
};

} // namespace bighero
