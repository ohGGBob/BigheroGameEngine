#pragma once
#include <cmath>
#include <cstddef>

namespace bighero {

// PointLight: an omni-directional light with position, range, intensity,
// colour and a quadratic inverse-square falloff. Standard-library only,
// self-contained.
class PointLight {
public:
    PointLight() {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }
    void SetRange(float r) { range_ = r < 0 ? 0 : r; }
    float Range() const { return range_; }
    void SetIntensity(float i) { intensity_ = i; }
    float Intensity() const { return intensity_; }
    void SetColor(float r, float g, float b) { cr_=r; cg_=g; cb_=b; }
    void Color(float& r, float& g, float& b) const { r=cr_; g=cg_; b=cb_; }

    // Quadratic inverse-square attenuation with a smooth cut at range.
    float Attenuation(float wx, float wy, float wz) const {
        float dx=wx-px_, dy=wy-py_, dz=wz-pz_;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (range_ <= 0) return 0.0f;
        float d = std::sqrt(d2);
        if (d > range_) return 0.0f;
        // Inverse-square with a soft clamp to avoid infinity.
        float atten = intensity_ / (1.0f + d2);
        // Optional range window: smooth fade to zero at range boundary.
        float fade = 1.0f - d/range_;
        if (fade < 0) fade = 0;
        return atten * fade * fade;
    }

private:
    float px_=0, py_=0, pz_=0;
    float range_=10.0f, intensity_=1.0f;
    float cr_=1, cg_=1, cb_=1;
};

} // namespace bighero
