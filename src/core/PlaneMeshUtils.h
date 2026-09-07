#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace bighero {

// Plane mesh generation utilities (different from sphere/box generators).
class PlaneMeshUtils {
public:
    struct Vertex { float x,y,z; float nx,ny,nz; float u,v;
        Vertex() : x(0),y(0),z(0),nx(0),ny(1),nz(0),u(0),v(0) {}
        Vertex(float px,float py,float pz) : x(px),y(py),z(pz),nx(0),ny(1),nz(0),u(0),v(0) {}
    };

    // Horizontal ground plane subdivided into gridX x gridZ cells.
    static void GenerateGrid(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                             float size, int gridX, int gridZ) {
        verts.clear(); indices.clear();
        if (gridX < 1) gridX = 1;
        if (gridZ < 1) gridZ = 1;
        float half = size / 2.0f;
        for (int z = 0; z <= gridZ; ++z)
            for (int x = 0; x <= gridX; ++x) {
                Vertex v;
                v.x = -half + (float)x / gridX * size;
                v.z = -half + (float)z / gridZ * size;
                v.y = 0;
                v.u = (float)x / gridX;
                v.v = (float)z / gridZ;
                verts.push_back(v);
            }
        int row = gridX + 1;
        for (int z = 0; z < gridZ; ++z)
            for (int x = 0; x < gridX; ++x) {
                uint32_t a = (uint32_t)(z*row + x), b = (uint32_t)(z*row + x+1);
                uint32_t c = (uint32_t)((z+1)*row + x), d = (uint32_t)((z+1)*row + x+1);
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(b); indices.push_back(c); indices.push_back(d);
            }
    }

    // Single quad (2 triangles) with given half-extents on the XZ plane.
    static void GenerateQuad(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                             float halfW, float halfD) {
        verts.clear(); indices.clear();
        Vertex v0(-halfW,0,-halfD), v1(halfW,0,-halfD), v2(halfW,0,halfD), v3(-halfW,0,halfD);
        v0.u=0;v0.v=0; v1.u=1;v1.v=0; v2.u=1;v2.v=1; v3.u=0;v3.v=1;
        verts.push_back(v0); verts.push_back(v1); verts.push_back(v2); verts.push_back(v3);
        indices.push_back(0); indices.push_back(2); indices.push_back(1);
        indices.push_back(0); indices.push_back(3); indices.push_back(2);
    }
};

} // namespace bighero
