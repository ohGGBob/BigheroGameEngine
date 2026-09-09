#pragma once
#include <cstdint>

namespace bighero {

// TileCoord: a 2D tile/grid cell coordinate with floored world-to-tile
// conversion, neighbors, and hash helpers.
struct TileCoord {
    int32_t tx = 0, ty = 0;

    TileCoord() = default;
    TileCoord(int32_t tx_, int32_t ty_) : tx(tx_), ty(ty_) {}

    bool operator==(const TileCoord& o) const { return tx == o.tx && ty == o.ty; }
    bool operator!=(const TileCoord& o) const { return !(*this == o); }

    TileCoord operator+(const TileCoord& o) const { return {tx + o.tx, ty + o.ty}; }
    TileCoord operator-(const TileCoord& o) const { return {tx - o.tx, ty - o.ty}; }

    // Convert a world position to a tile index given tile size in world units.
    static TileCoord FromWorld(double wx, double wy, double tileSize) {
        if (tileSize <= 0) tileSize = 1;
        return TileCoord((int32_t)std::floor(wx / tileSize),
                         (int32_t)std::floor(wy / tileSize));
    }
    // 4-neighbor at index 0..3.
    TileCoord Neighbor(int i) const {
        switch (i & 3) {
            case 0: return {tx + 1, ty};
            case 1: return {tx - 1, ty};
            case 2: return {tx, ty + 1};
            default:return {tx, ty - 1};
        }
    }
    uint64_t Hash() const {
        uint64_t h = (uint64_t)(uint32_t)tx * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t)(uint32_t)ty * 0xC2B2AE3D27D4EB4Full;
        h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
        return h;
    }

private:
    static double floor(double v) { return (double)(int64_t)v; }
};

} // namespace bighero
