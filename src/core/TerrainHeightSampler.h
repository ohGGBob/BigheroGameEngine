#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace bighero {

// TerrainHeightSampler: composes an arbitrary multi-octave noise into a
// heightfield and exposes height/steepness/normal queries. Standard-library
// only, self-contained (works with any 2D noise functor providing
// Noise(x,y) in [0,1]).
class TerrainHeightSampler {
public:
    TerrainHeightSampler(float amplitude = 10.0f, float baseFrequency = 0.02f)
        : amplitude_(amplitude), baseFrequency_(baseFrequency) {}

    void SetAmplitude(float a) { amplitude_ = a; }
    float Amplitude() const { return amplitude_; }
    void SetBaseFrequency(float f) { baseFrequency_ = f < 1e-6f ? 1e-6f : f; }

    template <typename NoiseFn>
    float Height(NoiseFn& noise, float x, float y) const {
        float n = noise.Noise(x * baseFrequency_, y * baseFrequency_);
        return n * amplitude_;
    }

    // Steepness (0..~1) based on finite-difference gradient magnitude.
    template <typename NoiseFn>
    float Steepness(NoiseFn& noise, float x, float y) const {
        const float e = 0.5f;
        float h0 = Height(noise, x, y);
        float hx = Height(noise, x + e, y);
        float hy = Height(noise, x, y + e);
        float gx = (hx - h0) / e;
        float gy = (hy - h0) / e;
        float mag = std::sqrt(gx * gx + gy * gy);
        return mag / (amplitude_ > 0 ? amplitude_ : 1.0f);
    }

    // Approximate surface normal (pointing up +y, normalized) at (x,y).
    template <typename NoiseFn>
    void Normal(NoiseFn& noise, float x, float y, float& nx, float& ny, float& nz) const {
        const float e = 0.5f;
        float h0 = Height(noise, x, y);
        float hx = Height(noise, x + e, y);
        float hy = Height(noise, x, y + e);
        float gx = (hx - h0) / e;
        float gy = (hy - h0) / e;
        float len = std::sqrt(gx * gx + gy * gy + 1.0f);
        nx = -gx / len; ny = 1.0f / len; nz = -gy / len;
    }

private:
    float amplitude_;
    float baseFrequency_;
};

} // namespace bighero
