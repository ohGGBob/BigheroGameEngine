#pragma once
#include <vector>
#include <algorithm>
#include "Color32.h"
#include <cmath>

namespace bighero {

// Gradient over Color32 stops (for lerped color ramps).
struct ColorGradient {
    struct Stop { float pos; Color32 color; };
    std::vector<Stop> stops;

    void AddStop(float pos, Color32 color) {
        stops.push_back({pos, color});
        std::sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b){ return a.pos < b.pos; });
    }
    void Clear() { stops.clear(); }
    bool Empty() const { return stops.empty(); }

    // Sample gradient at t in [0,1].
    Color32 Sample(float t) const {
        if (stops.empty()) return Color32::White();
        if (t <= stops.front().pos) return stops.front().color;
        if (t >= stops.back().pos)  return stops.back().color;
        for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
            const Stop& s0 = stops[i];
            const Stop& s1 = stops[i+1];
            if (t >= s0.pos && t <= s1.pos) {
                float f = (s1.pos > s0.pos) ? (t - s0.pos) / (s1.pos - s0.pos) : 0.0f;
                return s0.color.Lerp(s1.color, f);
            }
        }
        return stops.back().color;
    }
};

} // namespace bighero
