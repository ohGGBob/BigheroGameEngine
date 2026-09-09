#pragma once
#include <cmath>

namespace bighero {

// FXAA: a post-process anti-aliasing descriptor. Holds the quality/edge
// thresholds used by the GPU shader; the CPU side only computes a luma
// contrast metric from a color sample. Pure config + helper.
class FXAA {
public:
    FXAA() {}

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetQuality(int q) { quality_ = q < 0 ? 0 : (q > 5 ? 5 : q); }
    int Quality() const { return quality_; }
    void SetEdgeThreshold(float t) { edgeThresh_ = t < 0 ? 0 : (t > 1 ? 1 : t); }
    float EdgeThreshold() const { return edgeThresh_; }
    void SetSpanMax(float s) { spanMax_ = s < 0 ? 0 : s; }
    float SpanMax() const { return spanMax_; }

    // Luma of a color (Rec.709).
    static float Luma(float r, float g, float b) {
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
    // Relative luma contrast between center and neighbor sample.
    float Contrast(float centerLuma, float neighborLuma) const {
        float c = std::fabs(centerLuma - neighborLuma);
        return c < edgeThresh_ ? 0.0f : c;
    }

    void Reset() { enabled_ = false; quality_ = 2; edgeThresh_ = 0.166f; spanMax_ = 8.0f; }

private:
    bool enabled_ = false;
    int quality_ = 2;
    float edgeThresh_ = 0.166f;
    float spanMax_ = 8.0f;
};

} // namespace bighero
