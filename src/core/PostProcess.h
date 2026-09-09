#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// PostProcess: a post-processing effect descriptor with an enable flag,
// intensity, and blend mode. Used by the compositor to build the post chain.
struct PostProcess {
    enum class Type { None, Bloom, ToneMapping, Gamma, Vignette, SSAO, DOF };
    enum class Blend { Add, Multiply, Replace, Alpha };

    Type type = Type::None;
    Blend blend = Blend::Replace;
    float intensity = 1.0f;
    bool enabled = true;
    uint32_t order = 0;   // chain ordering

    PostProcess() = default;
    explicit PostProcess(Type type_, float intensity_ = 1.0f)
        : type(type_), intensity(intensity_) {}

    void SetEnabled(bool e) { enabled = e; }
    void SetIntensity(float i) { intensity = i < 0 ? 0 : i; }
    // Standard bloom threshold curve (soft knee).
    static float SoftThreshold(float v, float threshold, float knee) {
        if (v <= threshold - knee) return 0;
        if (v >= threshold + knee) return v;
        float d = v - (threshold - knee);
        return d * d / (4 * knee);
    }
};

// Vignette: a reusable vignette intensity evaluation (pure math helper).
struct Vignette {
    float strength = 0.5f;
    float radius = 1.0f;
    bool enabled = true;

    Vignette() = default;

    // Evaluate vignette factor at normalized screen position (nx,ny in [-1,1]).
    float Factor(float nx, float ny) const {
        float d = std::sqrt(nx * nx + ny * ny) / radius;
        if (d >= 1) return 0;
        float f = 1 - d * d;
        return f * strength;
    }
};

} // namespace bighero
