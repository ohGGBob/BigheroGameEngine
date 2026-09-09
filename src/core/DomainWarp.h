#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// DomainWarp: warps the input coordinates before sampling a 2D noise
// function, producing organic, turbulent patterns. Standard-library only,
// self-contained.
class DomainWarp {
public:
    DomainWarp(float strength = 1.0f, float frequency = 1.0f)
        : strength_(strength), frequency_(frequency) {}

    void SetStrength(float s) { strength_ = s; }
    float Strength() const { return strength_; }
    void SetFrequency(float f) { frequency_ = f < 1e-6f ? 1e-6f : f; }
    float Frequency() const { return frequency_; }

    // Warp (x,y) using two noise offsets; returns warped coords.
    template <typename NoiseFn>
    void Warp(NoiseFn& noise, float x, float y, float& ox, float& oy) const {
        float qx = noise.Noise(x * frequency_, y * frequency_);
        float qy = noise.Noise(x * frequency_ + 100.0f, y * frequency_ + 100.0f);
        float wx = strength_ * qx;
        float wy = strength_ * qy;
        ox = x + wx;
        oy = y + wy;
    }

    // Directly sample a noise field through the warp.
    template <typename NoiseFn>
    float Sample(NoiseFn& noise, float x, float y) const {
        float wx, wy;
        Warp(noise, x, y, wx, wy);
        return noise.Noise(wx, wy);
    }

private:
    float strength_, frequency_;
};

} // namespace bighero
