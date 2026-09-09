#pragma once
#include <cstdint>

namespace bighero {

// MeshVertex: describes the layout of a single mesh vertex (position,
// normal, uv, color, tangent) and how to pack it into a vertex buffer.
// Self-contained, std-lib only.
struct MeshVertex {
    float posX = 0, posY = 0, posZ = 0;
    float normalX = 0, normalY = 0, normalZ = 0;
    float uvX = 0, uvY = 0;
    float r = 1, g = 1, b = 1, a = 1;
    float tanX = 1, tanY = 0, tanZ = 0;   // tangent (w is handedness in others)

    MeshVertex() = default;
    MeshVertex(float px, float py, float pz)
        : posX(px), posY(py), posZ(pz) {}

    // Byte size of a tightly-packed vertex (16 floats = 64 bytes).
    static constexpr int Size() { return 16 * 4; }

    void SetPosition(float x, float y, float z) { posX = x; posY = y; posZ = z; }
    void SetNormal(float x, float y, float z) { normalX = x; normalY = y; normalZ = z; }
    void SetUV(float u, float v) { uvX = u; uvY = v; }
    void SetColor(float r_, float g_, float b_, float a_ = 1) {
        r = r_; g = g_; b = b_; a = a_;
    }
    // Triangle-winding flip when negative scale is applied to the mesh.
    bool IsDegenerate() const {
        return (posX == 0 && posY == 0 && posZ == 0) &&
               (normalX == 0 && normalY == 0 && normalZ == 0);
    }
};

} // namespace bighero
