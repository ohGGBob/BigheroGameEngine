#pragma once
#include <cstdint>

namespace bighero {

// ChunkCoord: a chunk-space coordinate used to identify column/chunk regions
// in a streamed world, with floored world-to-chunk conversion.
struct ChunkCoord {
    int32_t cx = 0, cy = 0, cz = 0;

    ChunkCoord() = default;
    ChunkCoord(int32_t cx_, int32_t cy_, int32_t cz_) : cx(cx_), cy(cy_), cz(cz_) {}

    bool operator==(const ChunkCoord& o) const {
        return cx == o.cx && cy == o.cy && cz == o.cz;
    }
    bool operator!=(const ChunkCoord& o) const { return !(*this == o); }

    // Convert a world coordinate to chunk coordinate given chunk size.
    static ChunkCoord FromWorld(int64_t wx, int64_t wy, int64_t wz, int64_t chunkSize) {
        if (chunkSize <= 0) chunkSize = 1;
        return ChunkCoord(
            (int32_t)FloorDiv(wx, chunkSize),
            (int32_t)FloorDiv(wy, chunkSize),
            (int32_t)FloorDiv(wz, chunkSize));
    }
    // Hash for unordered containers.
    uint64_t Hash() const {
        uint64_t h = (uint64_t)(uint32_t)cx * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t)(uint32_t)cy * 0xC2B2AE3D27D4EB4Full;
        h ^= (uint64_t)(uint32_t)cz * 0x165667B19E3779F9ull;
        h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
        return h;
    }

private:
    static int64_t FloorDiv(int64_t a, int64_t b) {
        int64_t q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }
};

} // namespace bighero
