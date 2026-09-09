#pragma once
#include <cmath>
#include <vector>

namespace bighero {

// Gradient2D: a 2D field that blends a set of gradient stops and can sample
// the blended color at a normalized position. Useful for sky/water/terrain
// tint maps. Standard-library only, self-contained.
class Gradient2D {
public:
    Gradient2D() = default;

    void AddStop(float pos, float r, float g, float b, float a = 1.0f) {
        stops_.push_back({pos, r, g, b, a});
        // Keep stops sorted by position for stable sampling.
        for (size_t i = stops_.size() - 1; i > 0; --i) {
            if (stops_[i].position < stops_[i - 1].position) {
                auto tmp = stops_[i];
                stops_[i] = stops_[i - 1];
                stops_[i - 1] = tmp;
            } else break;
        }
    }
    void Clear() { stops_.clear(); }
    size_t Count() const { return stops_.size(); }

    // Sample the gradient at t in [0,1]; returns interpolated RGBA.
    void Sample(float t, float& r, float& g, float& b, float& a) const {
        if (stops_.empty()) { r = g = b = 1; a = 1; return; }
        if (stops_.size() == 1) {
            r = stops_[0].r; g = stops_[0].g; b = stops_[0].b; a = stops_[0].a;
            return;
        }
        if (t <= stops_.front().position) {
            r = stops_.front().r; g = stops_.front().g;
            b = stops_.front().b; a = stops_.front().a; return;
        }
        if (t >= stops_.back().position) {
            r = stops_.back().r; g = stops_.back().g;
            b = stops_.back().b; a = stops_.back().a; return;
        }
        for (size_t i = 0; i + 1 < stops_.size(); ++i) {
            if (t >= stops_[i].position && t <= stops_[i + 1].position) {
                float span = stops_[i + 1].position - stops_[i].position;
                float f = span > 1e-9f ? (t - stops_[i].position) / span : 0.0f;
                r = Lerp(stops_[i].r, stops_[i + 1].r, f);
                g = Lerp(stops_[i].g, stops_[i + 1].g, f);
                b = Lerp(stops_[i].b, stops_[i + 1].b, f);
                a = Lerp(stops_[i].a, stops_[i + 1].a, f);
                return;
            }
        }
        r = stops_.back().r; g = stops_.back().g;
        b = stops_.back().b; a = stops_.back().a;
    }

private:
    struct Stop { float position, r, g, b, a; };
    static float Lerp(float x, float y, float f) { return x + (y - x) * f; }
    std::vector<Stop> stops_;
};

} // namespace bighero
