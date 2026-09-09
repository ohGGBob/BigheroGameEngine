#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// EllipseGeometry: generates a polyline circle/ellipse tessellation (centred
// at origin, with radii rx, ry) as a flat interleaved (x,y) vertex list.
// Standard-library only, self-contained.
class EllipseGeometry {
public:
    EllipseGeometry() {}
    EllipseGeometry(float rx, float ry, int segments = 32, float startAngle = 0,
                    float endAngle = 6.2831853f)
        { Generate(rx, ry, segments, startAngle, endAngle); }

    void Generate(float rx, float ry, int segments, float startAngle = 0,
                  float endAngle = 6.2831853f) {
        vertices_.clear();
        if (segments < 3) segments = 3;
        if (rx < 0) rx = 0;
        if (ry < 0) ry = 0;
        for (int i = 0; i <= segments; ++i) {
            float t = startAngle + (endAngle - startAngle) * (float)i / segments;
            float x = rx * std::cos(t);
            float y = ry * std::sin(t);
            vertices_.push_back(x);
            vertices_.push_back(y);
        }
    }

    const std::vector<float>& Vertices() const { return vertices_; }
    std::size_t VertexCount() const { return vertices_.size()/2; }

    // Perimeter estimate by integrating the polyline.
    float Perimeter() const {
        std::size_t n = VertexCount();
        float total = 0;
        for (std::size_t i = 0; i+1 < n; ++i) {
            float dx = vertices_[2*(i+1)] - vertices_[2*i];
            float dy = vertices_[2*(i+1)+1] - vertices_[2*i+1];
            total += std::sqrt(dx*dx + dy*dy);
        }
        return total;
    }

private:
    std::vector<float> vertices_;
};

} // namespace bighero
