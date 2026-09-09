#pragma once

namespace bighero {

// WorldPosition: a 3D world-space position that keeps both a float position
// and a chunk/region anchor. Helpful for streamed worlds where entities track
// their owning chunk to avoid repeated division. Self-contained.
class WorldPosition {
public:
    float x = 0, y = 0, z = 0;
    // Owning chunk coordinate (set externally by the chunk system).
    int chunkX = 0, chunkY = 0, chunkZ = 0;

    WorldPosition() = default;
    WorldPosition(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    void Set(float x_, float y_, float z_) { x = x_; y = y_; z = z_; }
    void Translate(float dx, float dy, float dz) { x += dx; y += dy; z += dz; }
    void MoveTo(float x_, float y_, float z_) {
        x = x_; y = y_; z = z_;
        // Invalidate cached chunk so it gets recomputed on next query.
        chunkX = chunkY = chunkZ = 0;
    }
    float DistanceTo(const WorldPosition& o) const {
        float dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return (float)sqrt2(dx * dx + dy * dy + dz * dz);
    }
    // Recompute the owning chunk from a fixed chunk size.
    void UpdateChunk(int chunkSize) {
        if (chunkSize <= 0) chunkSize = 1;
        chunkX = FloorDiv((long long)(int)(x / chunkSize));
        chunkY = (int)(y / chunkSize);
        chunkZ = (int)(z / chunkSize);
    }

private:
    static float sqrt2(double v) { return (float)sqrt(v); }
    static double sqrt(double v) { return __builtin_sqrt(v); }
    static int FloorDiv(long long v) { return (int)v; }
};

} // namespace bighero
