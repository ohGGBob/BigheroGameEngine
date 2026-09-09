#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// TemporalAA: temporal anti-aliasing state — stores the previous-frame
// color/history blend and motion-vector jitter offsets. Provides a
// blending weight for the shader. Pure config/state container.
class TemporalAA {
public:
    struct HistoryColor { float r, g, b; };

    TemporalAA() {}

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetSampleCount(int n) { samples_ = n < 1 ? 1 : n; }
    int SampleCount() const { return samples_; }
    void SetBlend(float b) { blend_ = b < 0 ? 0 : (b > 1 ? 1 : b); }
    float Blend() const { return blend_; }

    void SetJitter(float x, float y) { jx_ = x; jy_ = y; }
    void Jitter(float& x, float& y) const { x = jx_; y = jy_; }

    // Blend new color with previous history, store and return blended.
    HistoryColor Accumulate(float r, float g, float b) {
        HistoryColor out;
        if (!enabled_) { out.r=r; out.g=g; out.b=b; history_=out; hasHistory_=false; return out; }
        if (!hasHistory_) {
            // First frame: no valid history yet, return input unchanged.
            out.r=r; out.g=g; out.b=b; history_=out; hasHistory_=true; return out;
        }
        out.r = history_.r*(1-blend_) + r*blend_;
        out.g = history_.g*(1-blend_) + g*blend_;
        out.b = history_.b*(1-blend_) + b*blend_;
        history_ = out;
        return out;
    }
    void ResetHistory() { history_ = {0,0,0}; hasHistory_ = false; }

private:
    bool enabled_ = false;
    int samples_ = 4;
    float blend_ = 0.8f;
    float jx_ = 0, jy_ = 0;
    HistoryColor history_ = {0,0,0};
    bool hasHistory_ = false;
};

} // namespace bighero
