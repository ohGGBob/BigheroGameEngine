#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// RidgedNoise: a ridged multifractal built over an injected 2D noise function,
// producing sharp crest lines. Standard-library only, self-contained.
class RidgedNoise {
public:
    RidgedNoise(int octaves = 5, float persistence = 0.5f, float lacunarity = 2.0f,
                float gain = 1.0f)
        : octaves_(octaves > 0 ? octaves : 1), persistence_(persistence),
          lacunarity_(lacunarity), gain_(gain) {}

    template <typename NoiseFn>
    float Sample(NoiseFn& noise, float x, float y) const {
        float total = 0.0f, amp = 0.5f, freq = 1.0f, weight = 1.0f, norm = 0.0f;
        for (int o = 0; o < octaves_; ++o) {
            float n = noise.Noise(x * freq, y * freq);
            // Ridged: invert the noise around 0.5 to get crests at edges.
            n = 1.0f - std::fabs(2.0f * n - 1.0f);
            n *= n;   // sharpen
            n *= weight;
            weight = n * gain_;
            if (weight > 1.0f) weight = 1.0f;
            total += n * amp;
            norm += amp;
            amp *= persistence_;
            freq *= lacunarity_;
        }
        return norm > 0 ? total / norm : 0.0f;
    }

private:
    int octaves_;
    float persistence_, lacunarity_, gain_;
};

} // namespace bighero
