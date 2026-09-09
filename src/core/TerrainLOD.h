#pragma once
#include <cstddef>
#include <vector>
#include <cmath>

namespace bighero {

// TerrainLOD: selects a level-of-detail level for a terrain chunk based on
// camera distance, with hysteresis and chunk-size aware thresholds.
// Standard-library only, self-contained.
class TerrainLOD {
public:
    TerrainLOD() {}
    TerrainLOD(float chunkSize, float nearDist, float farDist, int maxLOD = 4)
        : chunkSize_(chunkSize > 0 ? chunkSize : 1.0f),
          nearDist_(nearDist), farDist_(farDist), maxLOD_(maxLOD < 0 ? 0 : maxLOD) {}

    void SetRanges(float nearDist, float farDist) {
        nearDist_ = nearDist;
        farDist_ = farDist > nearDist ? farDist : nearDist;
    }
    void SetMaxLOD(int m) { maxLOD_ = m < 0 ? 0 : m; }
    int MaxLOD() const { return maxLOD_; }
    float ChunkSize() const { return chunkSize_; }

    // LOD level for a chunk at the given camera distance (0 = most detailed).
    int Select(float cameraDist) const {
        if (cameraDist <= nearDist_) return 0;
        if (cameraDist >= farDist_) return maxLOD_;
        float t = (cameraDist - nearDist_) / (farDist_ - nearDist_);
        int level = (int)std::ceil(t * maxLOD_);
        if (level > maxLOD_) level = maxLOD_;
        if (level < 0) level = 0;
        return level;
    }

    // Hysteresis version: only change level if the new level differs by more
    // than `threshold` from the current (prevents flicker).
    int SelectHysteresis(float cameraDist, int current, int threshold = 1) const {
        int desired = Select(cameraDist);
        if (std::abs(desired - current) >= threshold) return desired;
        return current;
    }

    // Screen-space error metric (approx): how many pixels the LOD's geometric
    // error spans, used to scale detail with camera distance.
    float ScreenError(float cameraDist, float geoError, float viewportHeight) const {
        if (cameraDist < 1e-3f) cameraDist = 1e-3f;
        return (geoError * viewportHeight) / (cameraDist * 2.0f);
    }

private:
    float chunkSize_ = 1.0f;
    float nearDist_ = 10.0f, farDist_ = 200.0f;
    int maxLOD_ = 4;
};

} // namespace bighero
