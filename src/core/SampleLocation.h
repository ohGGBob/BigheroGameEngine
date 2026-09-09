#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// SampleLocation: a sample location within a sample pattern (x,y in [0,1)).
// Self-contained, std-lib only.
class SampleLocation {
public:
    SampleLocation() = default;
    SampleLocation(float x, float y) : x_(x), y_(y) {}

    void SetX(float x) { x_ = x; }
    float X() const { return x_; }
    void SetY(float y) { y_ = y; }
    float Y() const { return y_; }
    void Set(float x, float y) { x_=x; y_=y; }

    bool IsValid() const { return x_>=0.0f && x_<=1.0f && y_>=0.0f && y_<=1.0f; }
    static SampleLocation Centroid() { return SampleLocation(0.5f, 0.5f); }
    static std::vector<SampleLocation> DefaultGrid(uint32_t n) {
        std::vector<SampleLocation> v;
        if (n==0) return v;
        float step = 1.0f / (float)n;
        for (uint32_t i=0;i<n;++i) for (uint32_t j=0;j<n;++j)
            v.emplace_back((i+0.5f)*step, (j+0.5f)*step);
        return v;
    }

private:
    float x_=0, y_=0;
};

} // namespace bighero
