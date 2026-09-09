#pragma once
#include <vector>
#include <cstddef>
#include <limits>

namespace bighero {

// LODGroup: manages level-of-detail levels for a mesh/renderable. Each level
// has a screen-size threshold (in normalized units); picks the active level
// based on a computed screen size. Pure CPU-side selection.
class LODGroup {
public:
    struct Level {
        float screenSize;   // threshold: pick this level when screenSize >= threshold
        float distance;     // associated cull distance
    };

    LODGroup() {}

    void AddLevel(float screenSize, float distance = 0.0f) {
        levels_.push_back({screenSize, distance});
        dirty_ = true;
    }
    std::size_t LevelCount() const { return levels_.size(); }

    void SetCullRatio(float ratio) {
        cullRatio_ = ratio < 0 ? 0 : (ratio > 1 ? 1 : ratio);
    }
    float CullRatio() const { return cullRatio_; }

    // Choose the active LOD index for a given screen size.
    // Returns the first (highest-detail) level whose threshold is met:
    // screenSize >= level.screenSize. If none met, returns the last (lowest
    // detail) level. Levels are assumed sorted descending by threshold.
    int SelectLOD(float screenSize) const {
        if (levels_.empty()) return -1;
        float thresh = screenSize * cullRatio_;
        for (std::size_t i = 0; i < levels_.size(); ++i)
            if (thresh >= levels_[i].screenSize) return (int)i;
        return (int)(levels_.size() - 1);
    }

    float DistanceFor(int lod) const {
        if (lod < 0 || (std::size_t)lod >= levels_.size()) return 0.0f;
        return levels_[(std::size_t)lod].distance;
    }

    bool IsSorted() const {
        if (dirty_) return false;
        for (std::size_t i = 1; i < levels_.size(); ++i) {
            // Want descending screenSize (LOD0 highest detail).
            if (levels_[i].screenSize > levels_[i - 1].screenSize) return false;
        }
        return true;
    }
    void SortLODs() {
        // insertion sort descending by screenSize
        for (std::size_t i = 1; i < levels_.size(); ++i) {
            Level key = levels_[i];
            std::size_t j = i;
            while (j > 0 && levels_[j - 1].screenSize < key.screenSize) {
                levels_[j] = levels_[j - 1];
                --j;
            }
            levels_[j] = key;
        }
        dirty_ = false;
    }

private:
    std::vector<Level> levels_;
    float cullRatio_ = 1.0f;
    bool dirty_ = false;
};

} // namespace bighero
