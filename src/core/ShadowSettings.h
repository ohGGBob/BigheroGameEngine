#pragma once
#include <cmath>

namespace bighero {

// ShadowSettings: shadow-mapping configuration (resolution, bias, cascades)
// consumed by the renderer to build shadow passes.
struct ShadowSettings {
    // Shadow map resolution.
    int mapSize = 2048;
    // Bias to avoid self-shadow acne.
    float normalBias = 0.02f;
    float depthBias = 0.0015f;
    // Number of cascades (CSM) for directional shadows.
    int cascadeCount = 4;
    // Shadow strength in [0,1].
    float strength = 1.0f;
    // Whether shadows are globally enabled.
    bool enabled = true;
    // Shadow distance (fade distance).
    float maxDistance = 100.0f;

    ShadowSettings() = default;

    void SetResolution(int size) { mapSize = size < 64 ? 64 : size; }
    void SetStrength(float s) { strength = s < 0 ? 0 : (s > 1 ? 1 : s); }
    // Cascade split distance for cascade i of cascadeCount, near/far plane.
    float CascadeSplit(int i, float near, float far) const {
        if (cascadeCount <= 1) return far;
        // Practical split scheme blend of linear and logarithmic.
        float p = (float)(i + 1) / (float)cascadeCount;
        float lin = near + (far - near) * p;
        float log = near * std::pow(far / near, p);
        return lin * 0.5f + log * 0.5f;
    }
};

} // namespace bighero
