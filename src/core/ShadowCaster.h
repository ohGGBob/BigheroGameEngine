#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// ShadowCaster: descriptor for a light/object that casts shadows into the
// shadow map. Holds a cascade index, cast flag, bias, and the projected range
// used by the shadow render pass. Pure config container.
class ShadowCaster {
public:
    ShadowCaster() {}
    explicit ShadowCaster(std::uint64_t id) : id_(id) {}

    void SetId(std::uint64_t id) { id_ = id; }
    std::uint64_t Id() const { return id_; }
    void SetCastShadows(bool c) { cast_ = c; }
    bool CastShadows() const { return cast_; }

    void SetCascadeIndex(int i) { cascade_ = i < 0 ? 0 : i; }
    int CascadeIndex() const { return cascade_; }
    void SetCascadeCount(int n) { cascadeCount_ = n < 1 ? 1 : n; }
    int CascadeCount() const { return cascadeCount_; }

    void SetBias(float b) { bias_ = b; }
    float Bias() const { return bias_; }
    void SetNormalBias(float n) { normalBias_ = n; }
    float NormalBias() const { return normalBias_; }

    void SetShadowStrength(float s) { strength_ = s < 0 ? 0 : (s > 1 ? 1 : s); }
    float ShadowStrength() const { return strength_; }
    void SetZnear(float z) { znear_ = z; }
    float Znear() const { return znear_; }
    void SetZfar(float z) { zfar_ = z; }
    float Zfar() const { return zfar_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Depth range in clip space for the projection.
    void SetDepthRange(float nearC, float farC) { dNear_ = nearC; dFar_ = farC; }
    void DepthRange(float& nearC, float& farC) const { nearC = dNear_; farC = dFar_; }

private:
    std::uint64_t id_ = 0;
    bool cast_ = true;
    int cascade_ = 0, cascadeCount_ = 1;
    float bias_ = 0.001f, normalBias_ = 0.02f;
    float strength_ = 1.0f;
    float znear_ = 0.1f, zfar_ = 100.0f;
    float dNear_ = 0.0f, dFar_ = 1.0f;
    bool enabled_ = true;
};

} // namespace bighero
