#pragma once
#include <cstddef>
#include <cstdint>

namespace bighero {

// MeshCollider: collider that uses a mesh's geometry (triangles) for actual
// collision. Tracks vertex index range and a convex flag. Pure CPU-side
// descriptor; backend/tests use it to feed a convex hull or triangle test.
class MeshCollider {
public:
    MeshCollider() {}
    MeshCollider(std::size_t meshId) : meshId_(meshId) {}

    void SetMeshId(std::size_t id) { meshId_ = id; }
    std::size_t MeshId() const { return meshId_; }
    void SetVertexRange(std::size_t start, std::size_t count) {
        vStart_=start; vCount_=count;
    }
    void VertexRange(std::size_t& start, std::size_t& count) const {
        start=vStart_; count=vCount_;
    }
    void SetTriangleRange(std::size_t start, std::size_t count) {
        tStart_=start; tCount_=count;
    }
    void TriangleRange(std::size_t& start, std::size_t& count) const {
        start=tStart_; count=tCount_;
    }

    void SetConvex(bool c) { convex_ = c; }
    bool IsConvex() const { return convex_; }
    void SetTrigger(bool t) { trigger_ = t; }
    bool IsTrigger() const { return trigger_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    void SetTriangleCount(std::size_t n) { triCount_ = n; }
    std::size_t TriangleCount() const { return triCount_; }
    void SetVertexCount(std::size_t n) { vertCount_ = n; }
    std::size_t VertexCount() const { return vertCount_; }

private:
    std::size_t meshId_ = 0;
    std::size_t vStart_=0, vCount_=0, tStart_=0, tCount_=0;
    std::size_t triCount_=0, vertCount_=0;
    bool convex_=false, trigger_=false, enabled_=true;
};

} // namespace bighero
