#pragma once
#include <cmath>

namespace bighero {

// DepthState: a depth-test/write state descriptor with comparison function
// and bias. Provides a function to test a fragment against the current depth.
struct DepthState {
    enum class Func { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

    Func depthFunc = Func::LessEqual;
    bool depthWrite = true;
    bool depthTest = true;
    // Depth bias (slope-scaled + unit) to mitigate z-fighting.
    float bias = 0.0f;
    float slopeScale = 1.0f;

    DepthState() = default;
    explicit DepthState(Func f, bool write = true) : depthFunc(f), depthWrite(write) {}

    // Test a pixel's depth against the existing value using the comparison.
    bool Test(float pixelDepth, float existingDepth) const {
        if (!depthTest) return true;
        if (pixelDepth < existingDepth) {
            return depthFunc == Func::Less || depthFunc == Func::LessEqual ||
                   depthFunc == Func::Always || depthFunc == Func::NotEqual;
        } else if (pixelDepth > existingDepth) {
            return depthFunc == Func::Greater || depthFunc == Func::GreaterEqual ||
                   depthFunc == Func::Always || depthFunc == Func::NotEqual;
        } else {
            return depthFunc == Func::LessEqual || depthFunc == Func::GreaterEqual ||
                   depthFunc == Func::Equal || depthFunc == Func::Always;
        }
    }
    // Apply slope-scaled bias to a depth value.
    float ApplyBias(float depth, float slope) const {
        return depth + bias + slope * slopeScale;
    }
};

} // namespace bighero
