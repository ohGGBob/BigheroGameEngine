#pragma once
#include <cstdint>

namespace bighero {

// 2D light source descriptor: color, intensity, range, and shape for the 2D
// lighting system. Pure data holder.
class Light2D {
public:
    enum class Shape { Point, Spot, Directional };

    Light2D() {}
    Light2D(float x, float y, uint32_t color, float intensity, float range)
        : x_(x), y_(y), color_(color), intensity_(intensity), range_(range) {}

    void SetPosition(float x, float y) { x_ = x; y_ = y; }
    void Position(float& x, float& y) const { x = x_; y = y_; }

    void SetColor(uint32_t c) { color_ = c; }
    uint32_t Color() const { return color_; }
    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }
    void SetRange(float r) { range_ = r < 0 ? 0 : r; }
    float Range() const { return range_; }

    void SetShape(Shape s) { shape_ = s; }
    Shape ShapeType() const { return shape_; }
    // Spot angle / directional direction (degrees or normalized vector).
    void SetAngle(float deg) { angle_ = deg; }
    float Angle() const { return angle_; }
    void SetSpotOuter(float deg) { spotOuter_ = deg; }
    float SpotOuter() const { return spotOuter_; }

    // Shadow casting toggle.
    void SetCastShadow(bool s) { castShadow_ = s; }
    bool CastShadow() const { return castShadow_; }

    bool IsValid() const { return intensity_ > 0 && range_ > 0; }

private:
    float x_ = 0, y_ = 0;
    uint32_t color_ = 0xFFFFFFFFu;
    float intensity_ = 1.0f;
    float range_ = 10.0f;
    Shape shape_ = Shape::Point;
    float angle_ = 0.0f;
    float spotOuter_ = 30.0f;
    bool castShadow_ = false;
};

} // namespace bighero
