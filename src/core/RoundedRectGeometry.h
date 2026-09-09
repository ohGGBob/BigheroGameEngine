#pragma once
#include <cstddef>
#include <vector>
#include <cmath>

namespace bighero {

// RoundedRectGeometry: generates a rounded-rectangle quad fan (corners as
// arcs). Pure CPU-side geometry builder writing triangle vertices.
class RoundedRectGeometry {
public:
    RoundedRectGeometry() {}
    RoundedRectGeometry(float w, float h, float radius, std::size_t cornerSegments = 4)
        : w_(w), h_(h), radius_(radius), segs_(cornerSegments < 1 ? 1 : cornerSegments) {}

    void SetSize(float w, float h) { w_=w; h_=h; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : (r > w_/2 || r > h_/2 ? (w_<h_?w_/2:h_/2) : r); }
    float Radius() const { return radius_; }
    void SetCornerSegments(std::size_t s) { segs_ = s < 1 ? 1 : s; }

    // Generate triangle vertices (x,y) into outVerts (as [x,y] pairs).
    std::size_t Generate(std::vector<float>& verts) const {
        verts.clear();
        float hw = w_*0.5f, hh = h_*0.5f;
        float r = radius_;
        auto pushCorner = [&](float cx, float cy, float startAng) {
            for (std::size_t s = 0; s <= segs_; ++s) {
                float a = startAng + (float)s / (float)segs_ * 3.14159265f * 0.5f;
                verts.push_back(cx + std::cos(a)*r);
                verts.push_back(cy + std::sin(a)*r);
            }
        };
        // top-right, bottom-right, bottom-left, top-left
        pushCorner( hw-r, -hh+r, 3.14159265f*1.5f);
        pushCorner( hw-r,  hh-r, 0.0f);
        pushCorner(-hw+r,  hh-r, 3.14159265f*0.5f);
        pushCorner(-hw+r, -hh+r, 3.14159265f);
        return verts.size()/2;
    }

private:
    float w_ = 1.0f, h_ = 1.0f, radius_ = 0.1f;
    std::size_t segs_ = 4;
};

} // namespace bighero
