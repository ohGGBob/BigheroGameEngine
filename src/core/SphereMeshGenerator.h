#pragma once
#include "MeshData.h"
#include <cmath>

namespace bighero {

// Generates a UV sphere mesh (latitude/longitude). Self-contained.
class SphereMeshGenerator {
public:
    static MeshData Generate(float radius, int latitudeSegs, int longitudeSegs) {
        MeshData mesh;
        if (latitudeSegs < 2) latitudeSegs = 2;
        if (longitudeSegs < 3) longitudeSegs = 3;

        // Top pole.
        mesh.AddVertex(0, radius, 0, 0, 1, 0, 0.5f, 0.0f);

        // Rings (excluding poles).
        for (int lat = 1; lat < latitudeSegs; ++lat) {
            float phi = 3.14159265f * (float)lat / (float)latitudeSegs; // 0..pi from top
            float y = std::cos(phi) * radius;
            float r = std::sin(phi) * radius;
            for (int lon = 0; lon < longitudeSegs; ++lon) {
                float theta = 6.2831853f * (float)lon / (float)longitudeSegs;
                float x = r * std::cos(theta);
                float z = r * std::sin(theta);
                float nx = x / radius, ny = y / radius, nz = z / radius;
                float u = (float)lon / (float)longitudeSegs;
                float v = (float)lat / (float)latitudeSegs;
                mesh.AddVertex(x, y, z, nx, ny, nz, u, v);
            }
        }

        // Bottom pole.
        mesh.AddVertex(0, -radius, 0, 0, -1, 0, 0.5f, 1.0f);

        const int ringStart = 1;
        const int ringCount = latitudeSegs - 1;
        const int topPole = 0;
        const int bottomPole = ringStart + ringCount * longitudeSegs;

        // Top fan.
        for (int lon = 0; lon < longitudeSegs; ++lon) {
            int a = ringStart + lon;
            int b = ringStart + (lon + 1) % longitudeSegs;
            mesh.AddTriangle(topPole, a, b);
        }

        // Middle quads.
        for (int lat = 0; lat < ringCount - 1; ++lat) {
            for (int lon = 0; lon < longitudeSegs; ++lon) {
                int cur0 = ringStart + lat * longitudeSegs + lon;
                int cur1 = ringStart + lat * longitudeSegs + (lon + 1) % longitudeSegs;
                int next0 = ringStart + (lat + 1) * longitudeSegs + lon;
                int next1 = ringStart + (lat + 1) * longitudeSegs + (lon + 1) % longitudeSegs;
                mesh.AddTriangle(cur0, cur1, next1);
                mesh.AddTriangle(cur0, next1, next0);
            }
        }

        // Bottom fan.
        int lastRingStart = ringStart + (ringCount - 1) * longitudeSegs;
        for (int lon = 0; lon < longitudeSegs; ++lon) {
            int a = lastRingStart + lon;
            int b = lastRingStart + (lon + 1) % longitudeSegs;
            mesh.AddTriangle(bottomPole, b, a);
        }

        return mesh;
    }
};

} // namespace bighero
