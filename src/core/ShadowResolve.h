#pragma once
#include <cstddef>
#include <cstdint>

namespace bighero {

// ShadowResolve: settings for a shadow filter/resolve (blur radius, PCF size,
// shadow strength). Holds hardware-agnostic parameters the shadow mat pass
// reads. Pure config container.
class ShadowResolve {
public:
    ShadowResolve() {}
    explicit ShadowResolve(float strength) : strength_(strength) {}

    void SetStrength(float s) { strength_ = s < 0 ? 0 : (s > 1 ? 1 : s); }
    float Strength() const { return strength_; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }
    void SetPcfSamples(int n) { pcf_ = n < 1 ? 1 : n; }
    int PcfSamples() const { return pcf_; }
    void SetBlurPasses(int n) { blurPasses_ = n < 0 ? 0 : n; }
    int BlurPasses() const { return blurPasses_; }
    void SetNormalBias(float n) { normalBias_ = n; }
    float NormalBias() const { return normalBias_; }
    void SetDepthBias(float d) { depthBias_ = d; }
    float DepthBias() const { return depthBias_; }

    void SetCascadeBlend(bool b) { cascadeBlend_ = b; }
    bool CascadeBlend() const { return cascadeBlend_; }
    void SetSoftShadows(bool s) { softShadows_ = s; }
    bool SoftShadows() const { return softShadows_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    void Reset() {
        strength_ = 1.0f; radius_ = 2.0f; pcf_ = 16;
        blurPasses_ = 1; normalBias_ = 0.02f; depthBias_ = 0.001f;
        cascadeBlend_ = true; softShadows_ = true; enabled_ = true;
    }

private:
    float strength_ = 1.0f, radius_ = 2.0f;
    int pcf_ = 16, blurPasses_ = 1;
    float normalBias_ = 0.02f, depthBias_ = 0.001f;
    bool cascadeBlend_ = true, softShadows_ = true, enabled_ = true;
};

} // namespace bighero
