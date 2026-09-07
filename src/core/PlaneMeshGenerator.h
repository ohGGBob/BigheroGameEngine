#pragma once
#include "MeshData.h"
#include <cmath>

namespace bighero {

// Generates a grid plane mesh in the XZ plane (y up). Self-contained.
class PlaneMeshGenerator {
public:
    static MeshData Generate(float width, float depth, int segmentsX, int segmentsZ) {
        MeshData mesh;
        if (segmentsX < 1) segmentsX = 1;
        if (segmentsZ < 1) segmentsZ = 1;
        float hw = width * 0.5f, hd = depth * 0.5f;

        for (int z = 0; z <= segmentsZ; ++z) {
            float worldZ = hd - (float)z / (float)segmentsZ * depth;
            for (int x = 0; x <= segmentsX; ++x) {
                float worldX = -hw + (float)x / (float)segmentsX * width;
                float u = (float)x / (float)segmentsX;
                float v = (float)z / (float)segmentsZ;
                mesh.AddVertex(worldX, 0.0f, worldZ,
                               0.0f, 1.0f, 0.0f,
                               u, v);
            }
        }

        int stride = segmentsX + 1;
        for (int z = 0; z < segmentsZ; ++z) {
            for (int x = 0; x < segmentsX; ++x) {
                std::uint32_t i0 = (std::uint32_t)(z * stride + x);
                std::uint32_t i1 = (std::uint32_t)(z * stride + x + 1);
                std::uint32_t i2 = (std::uint32_t)((z + 1) * stride + x);
                std::uint32_t i3 = (std::uint32_t)((z + 1) * stride + x + 1);
                mesh.AddTriangle(i0, i2, i1);
                mesh.AddTriangle(i1, i2, i3);
            }
        }
        return mesh;
    }
};

} // namespace bighero
