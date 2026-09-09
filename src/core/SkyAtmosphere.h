#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// SkyAtmosphere: a physically-inspired sky atmosphere descriptor. Holds
// Rayleigh/Mie scattering coefficients, sun direction, exposure, and sky
// radius. CPU-side constants that drive the sky shader. Pure config.
class SkyAtmosphere {
public:
    SkyAtmosphere() {}

    void SetSunDirection(float dx, float dy, float dz) {
        float len = std::sqrt(dx*dx+dy*dy+dz*dz);
        if (len > 0) { dx_=dx/len; dy_=dy/len; dz_=dz/len; }
        else { dx_=0; dy_=1; dz_=0; }
    }
    void SunDirection(float& dx, float& dy, float& dz) const { dx=dx_; dy=dy_; dz=dz_; }
    void SetSunIntensity(float i) { sunI_ = i < 0 ? 0 : i; }
    float SunIntensity() const { return sunI_; }

    void SetRayleigh(float r, float g, float b) { rr_=r; rg_=g; rb_=b; }
    void SetMie(float m) { mie_ = m < 0 ? 0 : m; }
    float Mie() const { return mie_; }
    void SetMieG(float g) { mieG_ = g; } // phase anisotropy [-1,1]

    void SetExposure(float e) { exposure_ = e < 0 ? 0 : e; }
    float Exposure() const { return exposure_; }
    void SetEarthRadius(float r) { earthR_ = r < 1 ? 1 : r; }
    float EarthRadius() const { return earthR_; }
    void SetAtmosphereHeight(float h) { atmH_ = h < 0 ? 0 : h; }
    float AtmosphereHeight() const { return atmH_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Simple "sky tint" heuristic based on sun elevation.
    void SkyTint(float& r, float& g, float& b) const {
        float elev = dy_; // sun y after normalization
        float t = elev < 0 ? 0 : (elev > 1 ? 1 : elev);
        r = rr_ * (0.3f + 0.7f * t);
        g = rg_ * (0.3f + 0.7f * t);
        b = rb_ * (0.4f + 0.6f * t);
    }

private:
    float dx_=0, dy_=1, dz_=0;
    float sunI_ = 1.0f;
    float rr_=0.2f, rg_=0.4f, rb_=0.9f;
    float mie_ = 0.01f;
    float mieG_ = 0.8f;
    float exposure_ = 1.0f;
    float earthR_ = 6371.0f;
    float atmH_ = 100.0f;
    bool enabled_ = false;
};

} // namespace bighero
