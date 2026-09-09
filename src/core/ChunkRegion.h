#pragma once
#include <cstdint>

namespace bighero {

// ChunkRegion: describes a rectangular (in 3D, axis-aligned box) region of
// chunks, used by the streaming system to mark loaded/visible/requested areas.
struct ChunkRegion {
    int32_t minX = 0, minY = 0, minZ = 0;
    int32_t maxX = 0, maxY = 0, maxZ = 0; // inclusive

    ChunkRegion() = default;
    ChunkRegion(int32_t minX_, int32_t minY_, int32_t minZ_,
                int32_t maxX_, int32_t maxY_, int32_t maxZ_)
        : minX(minX_), minY(minY_), minZ(minZ_),
          maxX(maxX_), maxY(maxY_), maxZ(maxZ_) {}

    int32_t CountX() const { return maxX - minX + 1; }
    int32_t CountY() const { return maxY - minY + 1; }
    int32_t CountZ() const { return maxZ - minZ + 1; }
    int64_t ChunkCount() const {
        return (int64_t)CountX() * CountY() * CountZ();
    }
    bool Contains(int32_t cx, int32_t cy, int32_t cz) const {
        return cx >= minX && cx <= maxX && cy >= minY && cy <= maxY &&
               cz >= minZ && cz <= maxZ;
    }
    // Expand the region outward by radius chunks on every face.
    void Expand(int32_t r) {
        minX -= r; minY -= r; minZ -= r;
        maxX += r; maxY += r; maxZ += r;
    }
    bool Valid() const { return minX <= maxX && minY <= maxY && minZ <= maxZ; }
};

} // namespace bighero
