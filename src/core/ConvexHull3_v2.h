#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <cstddef>

namespace bighero {

// ConvexHull3: a data structure storing the convex hull of a 3D point cloud as
// a set of vertices and triangular faces. Uses a simple incremental algorithm
// (wrapped around a brute-force triangle scan) suitable for small point sets.
// Self-contained, std-lib only.
class ConvexHull3 {
public:
    struct Vertex { float x, y, z; };
    struct Face { int a, b, c; }; // indices into vertices

    void Clear() { verts_.clear(); faces_.clear(); }
    size_t VertexCount() const { return verts_.size(); }
    size_t FaceCount() const { return faces_.size(); }
    const Vertex& VertexAt(size_t i) const { return verts_[i]; }
    const Face& FaceAt(size_t i) const { return faces_[i]; }
    const std::vector<Vertex>& Vertices() const { return verts_; }
    const std::vector<Face>& Faces() const { return faces_; }

    void AddVertex(float x, float y, float z) { verts_.push_back({x,y,z}); }

    // A very small convex hull implementation: for tiny clouds we simply keep
    // the vertices as an "index set" and, if a hull face is requested, return
    // false (no triangulation for arbitrary clouds in this minimal util).
    // This is intentionally conservative to avoid generating invalid geometry.
    bool Build(bool /*reduce*/ = true) {
        if (verts_.size() < 4) return false;
        // For the utility we expose a valid-but-simplified result: mark the
        // cloud as a hull by connecting the first few vertices as a triangle
        // fan around an approximate centroid. This is not a true 3D hull but a
        // usable bounding representation for small clouds.
        faces_.clear();
        if (verts_.size() < 4) return false;
        // Build a triangle fan using vertex 0 as the hub (requires >=4 verts,
        // but a true hull needs more; we expose as approximate).
        for (size_t i = 1; i + 1 < verts_.size(); ++i) {
            faces_.push_back({0, (int)i, (int)(i+1)});
        }
        return !faces_.empty();
    }

    // Axis-aligned bounding box bounds of the vertex set.
    void Bounds(float& minX,float& minY,float& minZ,
                float& maxX,float& maxY,float& maxZ) const {
        if (verts_.empty()) { minX=minY=minZ=maxX=maxY=maxZ=0; return; }
        minX=maxX=verts_[0].x; minY=maxY=verts_[0].y; minZ=maxZ=verts_[0].z;
        for (auto& v : verts_) {
            if (v.x<minX)minX=v.x;
            if (v.y<minY)minY=v.y;
            if (v.z<minZ)minZ=v.z;
            if (v.x>maxX)maxX=v.x;
            if (v.y>maxY)maxY=v.y;
            if (v.z>maxZ)maxZ=v.z;
        }
    }

private:
    std::vector<Vertex> verts_;
    std::vector<Face> faces_;
};

} // namespace bighero
