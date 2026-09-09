#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// ChunkStreamer: manages a rolling set of streaming chunks (e.g. terrain /
// world tiles) around a moving focus point, loading/unloading by distance.
// Standard-library only, self-contained. Tracks chunk keys and their distance
// from the focus to decide which are active.
class ChunkStreamer {
public:
    ChunkStreamer() {}
    ChunkStreamer(float loadRadius, float unloadRadius, float chunkSize)
        : loadR_(loadRadius), unloadR_(unloadRadius > loadRadius ? unloadRadius : loadRadius),
          chunkSize_(chunkSize > 0 ? chunkSize : 1.0f) {}

    void SetRadii(float loadR, float unloadR) {
        loadR_ = loadR;
        unloadR_ = unloadR > loadR ? unloadR : loadR;
    }
    float LoadRadius() const { return loadR_; }
    float UnloadRadius() const { return unloadR_; }
    float ChunkSize() const { return chunkSize_; }

    // Compute the integer chunk coordinate containing a world position.
    void WorldToChunk(float wx, float wz, long& cx, long& cz) const {
        cx = (long)std::floor(wx / chunkSize_);
        cz = (long)std::floor(wz / chunkSize_);
    }

    // Return the set of active chunk coords (within loadR_) given a focus.
    void ActiveChunks(float wx, float wz, std::vector<long>& outCx,
                      std::vector<long>& outCz) const {
        outCx.clear(); outCz.clear();
        long fcx, fcz; WorldToChunk(wx, wz, fcx, fcz);
        int reach = (int)std::ceil(loadR_ / chunkSize_);
        for (int dz = -reach; dz <= reach; ++dz)
            for (int dx = -reach; dx <= reach; ++dx) {
                long cx = fcx + dx, cz = fcz + dz;
                float wx2 = (cx + 0.5f) * chunkSize_;
                float wz2 = (cz + 0.5f) * chunkSize_;
                float dxw = wx2 - wx, dzw = wz2 - wz;
                if (std::sqrt(dxw*dxw + dzw*dzw) <= loadR_) {
                    outCx.push_back(cx); outCz.push_back(cz);
                }
            }
    }

    // Whether a chunk should be unloaded given its distance from focus.
    bool ShouldUnload(float wx, float wz, float chunkCx, float chunkCz) const {
        float cx = (chunkCx + 0.5f) * chunkSize_;
        float cz = (chunkCz + 0.5f) * chunkSize_;
        float dx = cx - wx, dz = cz - wz;
        return std::sqrt(dx*dx + dz*dz) > unloadR_;
    }

private:
    float loadR_ = 32.0f, unloadR_ = 64.0f, chunkSize_ = 16.0f;
};

} // namespace bighero
