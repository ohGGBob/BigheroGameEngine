#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// Grid3D: a sparse-friendly 3D integer grid used for voxel/world chunk
// indexing, with hashing, neighbor enumeration, and distance helpers.
struct Grid3D {
    int x = 0, y = 0, z = 0;

    Grid3D() = default;
    Grid3D(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}

    Grid3D operator+(const Grid3D& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Grid3D operator-(const Grid3D& o) const { return {x - o.x, y - o.y, z - o.z}; }
    bool operator==(const Grid3D& o) const { return x == o.x && y == o.y && z == o.z; }

    int SqrDistance(const Grid3D& o) const {
        int dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return dx * dx + dy * dy + dz * dz;
    }
    // Distance in Chebyshev metric (max axis delta), for octree/chunk rings.
    int Chebyshev(const Grid3D& o) const {
        int dx = std::abs(x - o.x), dy = std::abs(y - o.y), dz = std::abs(z - o.z);
        return dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
    }
    // Compact coordinate hash for spatial hashing.
    uint64_t Hash() const {
        uint64_t h = (uint64_t)(uint32_t)x * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t)(uint32_t)y * 0xC2B2AE3D27D4EB4Full;
        h ^= (uint64_t)(uint32_t)z * 0x165667B19E3779F9ull;
        h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 33;
        return h;
    }
    // 6-face neighbor at index 0..5 (for BFS/voxel face adjacency).
    Grid3D Neighbor(int i) const {
        switch (i & 5) {
            case 0: return {x + 1, y, z};
            case 1: return {x - 1, y, z};
            case 2: return {x, y + 1, z};
            case 3: return {x, y - 1, z};
            case 4: return {x, y, z + 1};
            default:return {x, y, z - 1};
        }
    }
};

} // namespace bighero
