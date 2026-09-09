#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace bighero {

// CanvasScaler: scales a UI canvas from a reference resolution. Holds the
// reference size, match width/height factor, and a computed scale. Pure
// config + helper; scaler factors produced by the reference resolution.
class CanvasScaler {
public:
    CanvasScaler() {}
    CanvasScaler(float refW, float refH) { SetReferenceResolution(refW, refH); }

    void SetReferenceResolution(float w, float h) {
        refW_ = w < 1 ? 1 : w; refH_ = h < 1 ? 1 : h;
    }
    void ReferenceResolution(float& w, float& h) const { w=refW_; h=refH_; }
    void SetMatchWidthOrHeight(float m) { match_ = m < 0 ? 0 : (m > 1 ? 1 : m); }
    float MatchWidthOrHeight() const { return match_; }
    void SetScaleFactor(float s) { scale_ = s <= 0 ? 1 : s; }
    float ScaleFactor() const { return scale_; }
    void SetDpi(float dpi) { dpi_ = dpi < 1 ? 1 : dpi; }
    float Dpi() const { return dpi_; }

    // Compute scale factor for a given logical screen size (constant-pixel fit).
    float ComputeScale(float screenW, float screenH) const {
        if (screenW < 1 || screenH < 1) return 1.0f;
        float logW = std::log(screenW / refW_);
        float logH = std::log(screenH / refH_);
        float logAvg = logW * (1.0f - match_) + logH * match_;
        return std::exp(logAvg);
    }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    float refW_ = 1920.0f, refH_ = 1080.0f;
    float match_ = 0.5f;
    float scale_ = 1.0f, dpi_ = 96.0f;
    bool enabled_ = true;
};

} // namespace bighero
