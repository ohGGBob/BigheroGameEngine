#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// SkyLight: environmental/ambient light descriptor for sky-based lighting.
// Carries color, intensity, skybox direction, and shadow softness hints.
class SkyLight {
public:
    SkyLight() {}

    void SetColor(float r, float g, float b, float a = 1.0f) {
        r_=r; g_=g; b_=b; a_=a;
    }
    void Color(float& r, float& g, float& b, float& a) const { r=r_; g=g_; b=b_; a=a_; }

    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }

    void SetDirection(float dx, float dy, float dz) {
        float len = sqrtf(dx*dx+dy*dy+dz*dz);
        if (len > 0) { dx_=dx/len; dy_=dy/len; dz_=dz/len; }
        else { dx_=0; dy_=1; dz_=0; }
    }
    void Direction(float& dx, float& dy, float& dz) const { dx=dx_; dy=dy_; dz=dz_; }

    void SetEquirectTexture(std::uint64_t id) { texId_ = id; }
    std::uint64_t EquirectTexture() const { return texId_; }

    void SetShadowSoftness(float s) { softness_ = s < 0 ? 0 : (s > 1 ? 1 : s); }
    float ShadowSoftness() const { return softness_; }
    void SetCastShadows(bool c) { castShadows_ = c; }
    bool CastShadows() const { return castShadows_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    float r_=0.5f, g_=0.5f, b_=0.5f, a_=1;
    float intensity_ = 1.0f;
    float dx_=0, dy_=1, dz_=0;
    std::uint64_t texId_ = 0;
    float softness_ = 0.5f;
    bool castShadows_ = true;
    bool enabled_ = false;
};

} // namespace bighero
