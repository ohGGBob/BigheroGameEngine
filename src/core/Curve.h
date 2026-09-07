#pragma once
#include <vector>
#include <cmath>

namespace bighero {

// Utility for sampling a polyline/curve as a sequence of points.
class Curve {
public:
    // Global Catmull-Rom spline interpolation through arbitrary points.
    static std::vector<std::pair<float,float>> CatmullRom(
        const std::vector<std::pair<float,float>>& pts, int samplesPerSegment) {
        std::vector<std::pair<float,float>> out;
        if (pts.size() < 2) { out = pts; return out; }
        if (samplesPerSegment < 1) samplesPerSegment = 1;
        out.push_back(pts[0]);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const auto& p0 = pts[i == 0 ? 0 : i - 1];
            const auto& p1 = pts[i];
            const auto& p2 = pts[i + 1];
            const auto& p3 = pts[(i + 2 < pts.size()) ? i + 2 : pts.size() - 1];
            for (int s = 1; s <= samplesPerSegment; ++s) {
                float t = (float)s / samplesPerSegment;
                float t2 = t * t, t3 = t2 * t;
                float x = 0.5f * (2*p1.first + (-p0.first + p2.first)*t +
                                  (2*p0.first - 5*p1.first + 4*p2.first - p3.first)*t2 +
                                  (-p0.first + 3*p1.first - 3*p2.first + p3.first)*t3);
                float y = 0.5f * (2*p1.second + (-p0.second + p2.second)*t +
                                  (2*p0.second - 5*p1.second + 4*p2.second - p3.second)*t2 +
                                  (-p0.second + 3*p1.second - 3*p2.second + p3.second)*t3);
                out.push_back({x, y});
            }
        }
        return out;
    }
};

} // namespace bighero
