#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// LensFlare: descriptor + sample container for a lens-flare effect. Holds the
// occlusion query id, intensity, and per-element flares (scale, color, offset).
// Pure data container used by the post/light compositor.
class LensFlare {
public:
    struct Element {
        float offset;   // displacement along the flare axis
        float scale;    // size multiplier
        float r, g, b;  // tint
        float intensity;
    };

    LensFlare() {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }
    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }

    void AddElement(float offset, float scale, float r, float g, float b, float intensity) {
        elements_.push_back({offset, scale, r, g, b, intensity});
    }
    std::size_t ElementCount() const { return elements_.size(); }
    bool GetElement(std::size_t i, Element& out) const {
        if (i >= elements_.size()) return false;
        out = elements_[i]; return true;
    }
    void ClearElements() { elements_.clear(); }

    void SetOcclusionQueryId(std::uint64_t q) { occlusionQuery_ = q; }
    std::uint64_t OcclusionQueryId() const { return occlusionQuery_; }

    // When occlusion drops intensity toward 0 the flare fades out.
    void ApplyOcclusion(float occludedFraction) {
        float f = occludedFraction < 0 ? 0 : (occludedFraction > 1 ? 1 : occludedFraction);
        effectiveIntensity_ = intensity_ * f;
    }
    float EffectiveIntensity() const { return effectiveIntensity_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    float px_=0, py_=0, pz_=0;
    float intensity_ = 1.0f, effectiveIntensity_ = 1.0f;
    std::uint64_t occlusionQuery_ = 0;
    bool enabled_ = true;
    std::vector<Element> elements_;
};

} // namespace bighero
