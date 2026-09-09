#pragma once
#include <cmath>

namespace bighero {

// DepthOfField: a CPU-side depth-of-field descriptor with focal distance,
// aperture, and circle-of-confusion evaluation helper. Self-contained.
struct DepthOfField {
    enum class Mode { None, Bokeh, Gaussian, FocusDistance };

    Mode mode = Mode::Gaussian;
    // Camera focal distance in world units (focus plane).
    float focalDistance = 10.0f;
    // Focal range width (aperture size driven by this).
    float focalRange = 5.0f;
    // Max blur radius in pixels.
    float maxBlur = 8.0f;
    // Overall strength multiplier.
    float strength = 1.0f;
    bool enabled = true;

    DepthOfField() = default;
    explicit DepthOfField(float focalDistance_, float maxBlur_ = 8.0f, bool enabled_ = true)
        : focalDistance(focalDistance_), maxBlur(maxBlur_), enabled(enabled_) {}

    // Circle-of-confusion radius in pixels for a fragment at given depth.
    // depth is the camera-space depth (positive, >= near).
    float CircleOfConfusion(float depth) const {
        float d = std::fabs(depth - focalDistance);
        float coc = d / focalRange * maxBlur;
        return coc < 0 ? 0 : (coc > maxBlur ? maxBlur : coc);
    }
    // Linear sampling radius for the DoF blur (in normalized [0,1]).
    float BlurT(float depth) const {
        return CircleOfConfusion(depth) / (maxBlur > 0 ? maxBlur : 1) * strength;
    }
};

} // namespace bighero
