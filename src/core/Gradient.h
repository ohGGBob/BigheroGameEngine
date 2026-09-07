#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace bighero {

// Multi-stop linear color gradient.
struct Gradient {
    struct Stop { float pos; float r, g, b, a; };
    std::vector<Stop> stops;

    void AddStop(float pos, float r, float g, float b, float a = 1.0f) {
        stops.push_back({pos, r, g, b, a});
        std::sort(stops.begin(), stops.end(), [](const Stop& s1, const Stop& s2){ return s1.pos < s2.pos; });
    }
    void Clear() { stops.clear(); }
    bool Empty() const { return stops.empty(); }

    // Sample gradient at [0,1], returns RGBA in [0,1].
    void Sample(float t, float& r, float& g, float& b, float& a) const {
        if (stops.empty()) { r=g=b=a=0; return; }
        if (t <= stops.front().pos) { r=stops.front().r; g=stops.front().g; b=stops.front().b; a=stops.front().a; return; }
        if (t >= stops.back().pos)  { const Stop& s=stops.back(); r=s.r; g=s.g; b=s.b; a=s.a; return; }
        // find surrounding stops
        for (size_t i = 0; i + 1 < stops.size(); ++i) {
            const Stop& s0 = stops[i];
            const Stop& s1 = stops[i+1];
            if (t >= s0.pos && t <= s1.pos) {
                float f = (s1.pos > s0.pos) ? (t - s0.pos) / (s1.pos - s0.pos) : 0.0f;
                r = s0.r + (s1.r - s0.r) * f;
                g = s0.g + (s1.g - s0.g) * f;
                b = s0.b + (s1.b - s0.b) * f;
                a = s0.a + (s1.a - s0.a) * f;
                return;
            }
        }
        const Stop& s=stops.back(); r=s.r; g=s.g; b=s.b; a=s.a;
    }
};

} // namespace bighero
