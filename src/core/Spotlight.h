#pragma once
#include <cmath>
#include <cstddef>

namespace bighero {

// Spotlight: a cone-shaped light with position, direction, range, inner/outer
// cone angles and intensity/colour. Standard-library only, self-contained.
class Spotlight {
public:
    Spotlight() {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }
    void SetDirection(float dx, float dy, float dz) {
        float len=std::sqrt(dx*dx+dy*dy+dz*dz);
        if (len<1e-9f){ dx_=0;dy_=0;dz_=1; return; }
        dx_=dx/len; dy_=dy/len; dz_=dz/len;
    }
    void Direction(float& dx, float& dy, float& dz) const { dx=dx_; dy=dy_; dz=dz_; }

    void SetRange(float r) { range_ = r < 0 ? 0 : r; }
    float Range() const { return range_; }
    void SetIntensity(float i) { intensity_ = i; }
    float Intensity() const { return intensity_; }
    void SetColor(float r, float g, float b) { cr_=r; cg_=g; cb_=b; }
    void Color(float& r, float& g, float& b) const { r=cr_; g=cg_; b=cb_; }

    void SetAngles(float innerDeg, float outerDeg) {
        inner_ = innerDeg < 0 ? 0 : (innerDeg > 90 ? 90 : innerDeg);
        outer_ = outerDeg < 0 ? 0 : (outerDeg > 90 ? 90 : outerDeg);
        if (outer_ < inner_) outer_ = inner_;
    }
    float InnerAngle() const { return inner_; }
    float OuterAngle() const { return outer_; }

    // Attenuation factor (0..1) for a world point, from range + cone test.
    float Attenuation(float wx, float wy, float wz) const {
        float dx=wx-px_, dy=wy-py_, dz=wz-pz_;
        float dist=std::sqrt(dx*dx+dy*dy+dz*dz);
        if (dist > range_) return 0.0f;
        // falloff by distance (linear-ish)
        float dFade = 1.0f - dist/range_;
        if (dist < 1e-6f) return dFade;
        // direction dot
        float dot = (dx*dx_ + dy*dy_ + dz*dz_) / dist;
        float cosInner = std::cos(inner_*3.14159265f/180.0f);
        float cosOuter = std::cos(outer_*3.14159265f/180.0f);
        if (dot < cosOuter) return 0.0f;
        if (dot >= cosInner) return dFade;
        float f = (dot - cosOuter) / (cosInner - cosOuter + 1e-6f);
        return dFade * f;
    }

private:
    float px_=0, py_=0, pz_=0;
    float dx_=0, dy_=0, dz_=1;
    float range_=10.0f, intensity_=1.0f;
    float cr_=1, cg_=1, cb_=1;
    float inner_=20.0f, outer_=30.0f;
};

} // namespace bighero
