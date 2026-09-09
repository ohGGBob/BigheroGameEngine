#pragma once
#include <cstdint>

namespace bighero {

// AreaLight: a rectangular / area light descriptor. Holds position, tangent
// size (width/height), direction, color, intensity, and a rotation in degrees
// around the normal. Used by the lighting backend to sample area shadowing.
class AreaLight {
public:
    enum class Shape { Rect, Disc };

    AreaLight() {}
    AreaLight(Shape shape) : shape_(shape) {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }
    void SetDirection(float dx, float dy, float dz) {
        float len = normalize(dx, dy, dz);
        if (len > 0) { dx_=dx/len; dy_=dy/len; dz_=dz/len; }
        else { dx_=0; dy_=1; dz_=0; }
    }
    void Direction(float& dx, float& dy, float& dz) const { dx=dx_; dy=dy_; dz=dz_; }

    void SetWidth(float w) { width_ = w < 0 ? 0 : w; }
    float Width() const { return width_; }
    void SetHeight(float h) { height_ = h < 0 ? 0 : h; }
    float Height() const { return height_; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }

    void SetColor(float r, float g, float b, float a = 1.0f) {
        r_=r; g_=g; b_=b; a_=a;
    }
    void Color(float& r, float& g, float& b, float& a) const { r=r_; g=g_; b=b_; a=a_; }
    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }
    void SetRange(float r) { range_ = r < 0 ? 0 : r; }
    float Range() const { return range_; }

    void SetShape(Shape s) { shape_ = s; }
    Shape CurrentShape() const { return shape_; }
    void SetRotation(float deg) { rot_ = deg; }
    float Rotation() const { return rot_; }
    void SetCastShadows(bool c) { castShadows_ = c; }
    bool CastShadows() const { return castShadows_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    static float normalize(float x, float y, float z) {
        // static helper to compute length (name kept for clarity)
        return sqrtf(x*x + y*y + z*z);
    }
    Shape shape_ = Shape::Rect;
    float px_=0, py_=0, pz_=0;
    float dx_=0, dy_=1, dz_=0;
    float width_=1, height_=1, radius_=1;
    float r_=1, g_=1, b_=1, a_=1;
    float intensity_ = 1.0f;
    float range_ = 20.0f;
    float rot_ = 0;
    bool castShadows_ = true;
    bool enabled_ = true;
};

} // namespace bighero
