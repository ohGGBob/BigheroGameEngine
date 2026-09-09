#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// Gjk2D: 2D Gilbert-Johnson-Keerthi boolean intersection test between two
// convex polygons. Pure CPU-side helper; self-contained.
class Gjk2D {
public:
    struct Vec2 { float x, y; };

    Gjk2D() {}

    bool Intersect(const std::vector<std::pair<float,float>>& a,
                   const std::vector<std::pair<float,float>>& b) const {
        if (a.size() < 3 || b.size() < 3) return false;
        // attempt separating axis on all edges of both polygons
        for (std::size_t i = 0; i < a.size(); ++i) {
            Vec2 e{ a[(i+1)%a.size()].first - a[i].first,
                    a[(i+1)%a.size()].second - a[i].second };
            Vec2 ax{ -e.y, e.x };
            if (Separates(a, b, ax)) return false;
        }
        for (std::size_t i = 0; i < b.size(); ++i) {
            Vec2 e{ b[(i+1)%b.size()].first - b[i].first,
                    b[(i+1)%b.size()].second - b[i].second };
            Vec2 ax{ -e.y, e.x };
            if (Separates(a, b, ax)) return false;
        }
        return true;
    }

private:
    static float Dot(float ax, float ay, const std::pair<float,float>& p) {
        return ax*p.first + ay*p.second;
    }
    bool Separates(const std::vector<std::pair<float,float>>& a,
                   const std::vector<std::pair<float,float>>& b,
                   const Vec2& axis) const {
        float amin=1e30f, amax=-1e30f, bmin=1e30f, bmax=-1e30f;
        for (auto& p : a) { float d = Dot(axis.x, axis.y, p); if (d<amin)amin=d; if (d>amax)amax=d; }
        for (auto& p : b) { float d = Dot(axis.x, axis.y, p); if (d<bmin)bmin=d; if (d>bmax)bmax=d; }
        return amax < bmin || bmax < amin;
    }
};

} // namespace bighero
