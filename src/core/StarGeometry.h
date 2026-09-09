#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// StarGeometry: tessellates an N-point star polygon (outer + inner radius) as
// a flat interleaved (x,y) outline. Standard-library only, self-contained.
class StarGeometry {
public:
    StarGeometry() {}
    StarGeometry(int points, float outerRadius, float innerRadius, float cx = 0,
                 float cy = 0, float rotation = 0)
        { Generate(points, outerRadius, innerRadius, cx, cy, rotation); }

    void Generate(int points, float outerRadius, float innerRadius, float cx = 0,
                  float cy = 0, float rotation = 0) {
        vertices_.clear();
        if (points < 2) points = 2;
        if (outerRadius < 0) outerRadius = 0;
        if (innerRadius < 0) innerRadius = 0;
        int count = points * 2;
        for (int i = 0; i < count; ++i) {
            float angle = rotation + 6.2831853f * (float)i / count - 1.5707963f;
            float r = (i % 2 == 0) ? outerRadius : innerRadius;
            float x = cx + r * std::cos(angle);
            float y = cy + r * std::sin(angle);
            vertices_.push_back(x);
            vertices_.push_back(y);
        }
        // close loop
        vertices_.push_back(vertices_[0]);
        vertices_.push_back(vertices_[1]);
    }

    const std::vector<float>& Vertices() const { return vertices_; }
    std::size_t VertexCount() const { return vertices_.size()/2; }

    // Approximate area assuming a regular star (n * rOuter * rInner * sin(pi/n)).
    float Area() const {
        if (points_ < 2) return 0;
        float n = (float)points_;
        return n * outer_ * inner_ * std::sin(3.14159265f / n);
    }
    void SetParams(int points, float outer, float inner) {
        points_ = points < 2 ? 2 : points; outer_=outer; inner_=inner;
    }

private:
    std::vector<float> vertices_;
    int points_ = 5; float outer_ = 1, inner_ = 0.5f;
};

} // namespace bighero
