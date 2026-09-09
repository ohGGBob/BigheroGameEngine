#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// OcclusionCuller: a software occlusion tracker that accumulates occluder
// coverage in a low-res 2D mask and lets you test whether a screen-space
// box is likely visible. Simplified visibility culling used for frustum +
// occluder heuristics. Pure stdlib.
class OcclusionCuller {
public:
    OcclusionCuller() {}
    OcclusionCuller(int width, int height) { Resize(width, height); }

    void Resize(int w, int h) {
        w_ = w < 1 ? 1 : w; h_ = h < 1 ? 1 : h;
        mask_.assign((std::size_t)w_ * h_, 0);
    }
    int Width() const { return w_; }
    int Height() const { return h_; }

    // Mark a screen-space box as an occluder (bump coverage).
    void MarkOccluder(float x0, float y0, float x1, float y1) {
        int ix0 = ClampToInt(x0, w_ - 1), ix1 = ClampToInt(x1, w_ - 1);
        int iy0 = ClampToInt(y0, h_ - 1), iy1 = ClampToInt(y1, h_ - 1);
        for (int y = iy0; y <= iy1; ++y)
            for (int x = ix0; x <= ix1; ++x)
                ++mask_[(std::size_t)y * w_ + x];
    }

    // Test whether a screen-space box is fully covered (occluded) or has any
    // visible (uncovered) areas. Returns true if the box is (mostly) occluded.
    bool IsOccluded(float x0, float y0, float x1, float y1) const {
        int ix0 = ClampToInt(x0, w_ - 1), ix1 = ClampToInt(x1, w_ - 1);
        int iy0 = ClampToInt(y0, h_ - 1), iy1 = ClampToInt(y1, h_ - 1);
        float covered = 0, total = 0;
        for (int y = iy0; y <= iy1; ++y) {
            for (int x = ix0; x <= ix1; ++x) {
                if (mask_[(std::size_t)y * w_ + x] > 0) ++covered;
                ++total;
            }
        }
        if (total == 0) return false;
        return (covered / total) > 0.9f; // >90% covered counts as occluded
    }

    void Reset() { std::fill(mask_.begin(), mask_.end(), 0); }

private:
    static int ClampToInt(float v, int maxV) {
        int i = (int)v;
        if (i < 0) i = 0;
        if (i > maxV) i = maxV;
        return i;
    }
    int w_ = 1, h_ = 1;
    std::vector<int> mask_;
};

} // namespace bighero
