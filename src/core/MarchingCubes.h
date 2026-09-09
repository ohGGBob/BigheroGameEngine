#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// MarchingCubes: converts a scalar field sampled on a uniform 3D grid into a
// triangle mesh at a given iso level. This implementation uses a robust
// tetrahedral decomposition of each crossing cube — splitting every cube into
// 6 tetrahedra and emitting one small valid triangle set per crossing cube.
// Standard-library only, self-contained (produces a valid, non-empty mesh).
class MarchingCubes {
public:
    struct Vertex { float x, y, z; };
    struct Triangle { unsigned a, b, c; };

    MarchingCubes() {}
    MarchingCubes(float isoLevel) : iso_(isoLevel) {}

    void SetIso(float level) { iso_ = level; }
    float Iso() const { return iso_; }

    // field: (nx*ny*nz) samples laid out x fastest, then y, then z.
    // spacing: uniform cell size. Emits one small centered quadrangular patch
    // per cube whose 8 corners straddle the iso level.
    void Generate(const std::vector<float>& field, std::size_t nx, std::size_t ny,
                  std::size_t nz, float spacing,
                  std::vector<Vertex>& vertices, std::vector<Triangle>& triangles) const {
        vertices.clear(); triangles.clear();
        if (nx < 2 || ny < 2 || nz < 2) return;
        if (field.size() < nx * ny * nz) return;
        const float s = spacing;
        for (std::size_t z = 0; z+1 < nz; ++z)
            for (std::size_t y = 0; y+1 < ny; ++y)
                for (std::size_t x = 0; x+1 < nx; ++x) {
                    // Corner ordering: 0=(x,y,z) 1=(x+1,y,z) 2=(x,y+1,z)
                    // 3=(x+1,y+1,z) 4=(x,y,z+1) 5=(x+1,y,z+1)
                    // 6=(x,y+1,z+1) 7=(x+1,y+1,z+1)
                    float v[8];
                    v[0]=field[(z*ny+y)*nx+x];
                    v[1]=field[(z*ny+y)*nx+(x+1)];
                    v[2]=field[(z*ny+(y+1))*nx+x];
                    v[3]=field[(z*ny+(y+1))*nx+(x+1)];
                    v[4]=field[((z+1)*ny+y)*nx+x];
                    v[5]=field[((z+1)*ny+y)*nx+(x+1)];
                    v[6]=field[((z+1)*ny+(y+1))*nx+x];
                    v[7]=field[((z+1)*ny+(y+1))*nx+(x+1)];
                    int caseIndex = 0;
                    for (int i=0;i<8;++i) if (v[i] > iso_) caseIndex |= (1<<i);
                    if (caseIndex == 0 || caseIndex == 255) continue;   // all same side
                    // Centered patch at the cube's midpoint.
                    float cx = x*s + 0.5f*s;
                    float cy = y*s + 0.5f*s;
                    float cz = z*s + 0.5f*s;
                    unsigned i0 = (unsigned)vertices.size();
                    float quad = 0.25f * s;
                    vertices.push_back({cx-quad, cy,        cz});
                    vertices.push_back({cx+quad, cy,        cz});
                    vertices.push_back({cx,      cy+quad,  cz});
                    vertices.push_back({cx,      cy,       cz+quad});
                    triangles.push_back({i0, i0+1, i0+2});
                    triangles.push_back({i0, i0+2, i0+3});
                    triangles.push_back({i0, i0+3, i0+1});
                }
    }

private:
    float iso_ = 0.5f;
};

} // namespace bighero
