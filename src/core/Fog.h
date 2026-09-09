#pragma once
#include <cmath>

namespace bighero {

// Fog: an exponential / linear / height fog descriptor with density, color,
// and linear range. Provides fog factor evaluation in pure math.
struct Fog {
    enum class Mode { Exp, ExpSquared, Linear };

    Mode mode = Mode::Exp;
    float density = 0.01f;
    float start = 10.0f, end = 300.0f;     // linear mode range
    float heightFalloff = 0.0f;            // 0 disables height fog
    float colorR = 0.6f, colorG = 0.7f, colorB = 0.8f;

    Fog() = default;

    // Evaluate fog factor at a given view distance (and optional height).
    float Factor(float distance, float height = 0.0f) const {
        if (distance <= 0) return 0;
        float f = 0;
        switch (mode) {
            case Mode::Exp:
                f = 1 - std::exp(-density * density * distance * distance);
                break;
            case Mode::ExpSquared:
                f = 1 - std::exp(-(density * distance) * (density * distance));
                break;
            case Mode::Linear: {
                if (distance <= start) f = 0;
                else if (distance >= end) f = 1;
                else f = (distance - start) / (end - start);
                break;
            }
        }
        if (heightFalloff > 0) {
            float hf = std::exp(-height * heightFalloff);
            f *= hf;
        }
        return f < 0 ? 0 : (f > 1 ? 1 : f);
    }
};

} // namespace bighero
