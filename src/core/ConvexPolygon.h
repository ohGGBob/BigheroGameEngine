#pragma once
#include <vector>
#include <cmath>

namespace bighero {

// ConvexPolygon: a 2D convex polygon with signed area, centroid, convexity
// test, containment, and winding normalization. Self-contained.
class ConvexPolygon {
public:
    ConvexPolygon() = default;
    explicit ConvexPolygon(std::vector<float> verts) : verts_(std::move(verts)) {}

    void SetVertices(std::vector<float> v) { verts_ = std::move(v); }
    size_t VertexCount() const { return verts_.size() / 2; }
    void Vertex(size_t i, float& x, float& y) const {
        x = verts_[i * 2]; y = verts_[i * 2 + 1];
    }

    // Signed area (positive when CCW).
    double SignedArea() const {
        size_t n = verts_.size() / 2;
        if (n < 3) return 0;
        double sum = 0;
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            sum += (double)verts_[i*2] * verts_[j*2+1]
                 - (double)verts_[j*2] * verts_[i*2+1];
        }
        return sum * 0.5;
    }
    // Ensure counter-clockwise winding.
    void EnsureCCW() {
        if (SignedArea() < 0) {
            std::vector<float> r(verts_.size());
            size_t n = verts_.size() / 2;
            for (size_t i = 0; i < n; ++i) {
                r[i*2]   = verts_[(n-1-i)*2];
                r[i*2+1] = verts_[(n-1-i)*2+1];
            }
            verts_ = std::move(r);
        }
    }
    bool IsConvex() const {
        size_t n = verts_.size() / 2;
        if (n < 4) return true;
        // All cross products must share the same sign.
        for (size_t i = 0; i < n; ++i) {
            size_t p = (i + n - 1) % n;
            size_t q = (i + 1) % n;
            float e1x = verts_[i*2] - verts_[p*2];
            float e1y = verts_[i*2+1] - verts_[p*2+1];
            float e2x = verts_[q*2] - verts_[i*2];
            float e2y = verts_[q*2+1] - verts_[i*2+1];
            float cross = e1x * e2y - e1y * e2x;
            if (cross < -1e-6f) return false;
        }
        return true;
    }
    // Point containment via cross-product sign consistency.
    bool Contains(float px, float py) const {
        size_t n = verts_.size() / 2;
        if (n < 3) return false;
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            float e1x = verts_[j*2] - verts_[i*2];
            float e1y = verts_[j*2+1] - verts_[i*2+1];
            float e2x = px - verts_[i*2];
            float e2y = py - verts_[i*2+1];
            if (e1x * e2y - e1y * e2x < -1e-6f) return false;
        }
        return true;
    }
    void Centroid(float& ox, float& oy) const {
        size_t n = verts_.size() / 2;
        if (n == 0) { ox = 0; oy = 0; return; }
        double a = 0, cx = 0, cy = 0;
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            double cross = (double)verts_[i*2] * verts_[j*2+1]
                         - (double)verts_[j*2] * verts_[i*2+1];
            a += cross;
            cx += (verts_[i*2] + verts_[j*2]) * cross;
            cy += (verts_[i*2+1] + verts_[j*2+1]) * cross;
        }
        if (std::fabs(a) < 1e-12) { ox = 0; oy = 0; return; }
        a *= 0.5;
        ox = (float)(cx / (6 * a));
        oy = (float)(cy / (6 * a));
    }

private:
    std::vector<float> verts_;
};

} // namespace bighero
