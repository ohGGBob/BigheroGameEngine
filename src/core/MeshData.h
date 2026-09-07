#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// CPU-side mesh data: interleaved positions/normals/uvs plus triangle indices.
// Pure standard library; consumers upload to GPU. Designed to be indexable.
class MeshData {
public:
    std::vector<float> positions;  // xyz per vertex
    std::vector<float> normals;    // xyz per vertex
    std::vector<float> uvs;        // uv per vertex
    std::vector<std::uint32_t> indices; // triangle indices (winding CCW)

    void Clear() {
        positions.clear(); normals.clear(); uvs.clear(); indices.clear();
    }

    std::size_t VertexCount() const { return positions.size() / 3; }
    std::size_t IndexCount() const { return indices.size(); }
    bool Empty() const { return positions.empty(); }

    void AddVertex(float px, float py, float pz,
                   float nx, float ny, float nz,
                   float u, float v) {
        positions.push_back(px); positions.push_back(py); positions.push_back(pz);
        normals.push_back(nx); normals.push_back(ny); normals.push_back(nz);
        uvs.push_back(u); uvs.push_back(v);
    }

    void AddTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        indices.push_back(a); indices.push_back(b); indices.push_back(c);
    }

    // Computes flat per-face normals from positions (replaces normals).
    void RecomputeNormals() {
        normals.assign(positions.size(), 0.0f);
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            std::uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
            float ax = P(ia, 0), ay = P(ia, 1), az = P(ia, 2);
            float bx = P(ib, 0), by = P(ib, 1), bz = P(ib, 2);
            float cx = P(ic, 0), cy = P(ic, 1), cz = P(ic, 2);
            float ux = bx - ax, uy = by - ay, uz = bz - az;
            float vx = cx - ax, vy = cy - ay, vz = cz - az;
            float nx = uy * vz - uz * vy;
            float ny = uz * vx - ux * vz;
            float nz = ux * vy - uy * vx;
            // Accumulate (area-weighted).
            AddNorm(ia, nx, ny, nz); AddNorm(ib, nx, ny, nz); AddNorm(ic, nx, ny, nz);
        }
        // Normalize per-vertex normals.
        for (std::size_t vid = 0; vid < VertexCount(); ++vid) {
            float nx = N(vid, 0), ny = N(vid, 1), nz = N(vid, 2);
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                normals[vid * 3 + 0] = nx / len;
                normals[vid * 3 + 1] = ny / len;
                normals[vid * 3 + 2] = nz / len;
            }
        }
    }

    float P(std::size_t vid, int comp) const { return positions[vid * 3 + comp]; }
    float N(std::size_t vid, int comp) const { return normals[vid * 3 + comp]; }
    float UV(std::size_t vid, int comp) const { return uvs[vid * 2 + comp]; }

private:
    void AddNorm(std::size_t vid, float nx, float ny, float nz) {
        normals[vid * 3 + 0] += nx;
        normals[vid * 3 + 1] += ny;
        normals[vid * 3 + 2] += nz;
    }
};

} // namespace bighero
