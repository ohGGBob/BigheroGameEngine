#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// ArcGeometry: tessellates a circular arc (centre, radius, angular span) as a
// flat interleaved (x,y) polyline. Standard-library only, self-contained.
class ArcGeometry {
public:
    ArcGeometry() {}
    ArcGeometry(float cx, float cy, float radius, float startAngle, float endAngle,
                int segments = 32)
        { Generate(cx, cy, radius, startAngle, endAngle, segments); }

    void Generate(float cx, float cy, float radius, float startAngle, float endAngle,
                  int segments = 32) {
        vertices_.clear();
        if (segments < 1) segments = 1;
        if (radius < 0) radius = 0;
        for (int i = 0; i <= segments; ++i) {
            float t = startAngle + (endAngle - startAngle) * (float)i / segments;
            float x = cx + radius * std::cos(t);
            float y = cy + radius * std::sin(t);
            vertices_.push_back(x);
            vertices_.push_back(y);
        }
    }

    const std::vector<float>& Vertices() const { return vertices_; }
    std::size_t VertexCount() const { return vertices_.size()/2; }

    // Arc length (radius * |sweep|).
    float Length(float radius) const {
        if (!sweepKnown_) return 0;
        return std::fabs(radius * sweep_);
    }

    void SetSweep(float sweep) { sweep_ = sweep; sweepKnown_ = true; }

private:
    std::vector<float> vertices_;
    float sweep_ = 0;
    bool sweepKnown_ = false;
};

} // namespace bighero
