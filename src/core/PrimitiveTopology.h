#pragma once
#include <cstdint>

namespace bighero {

// PrimitiveTopology: describes how a vertex list is interpreted when drawing.
// Self-contained, std-lib only.
class PrimitiveTopology {
public:
    enum class Topology : uint8_t {
        PointList = 0,
        LineList = 1,
        LineStrip = 2,
        TriangleList = 3,
        TriangleStrip = 4,
        TriangleFan = 5,
        LineListAdjacency = 6,
        TriangleListAdjacency = 7,
        PatchList = 8
    };

    PrimitiveTopology() = default;
    explicit PrimitiveTopology(Topology t) : topology_(t) {}

    static PrimitiveTopology Points() { return PrimitiveTopology(Topology::PointList); }
    static PrimitiveTopology Lines() { return PrimitiveTopology(Topology::LineList); }
    static PrimitiveTopology Triangles() { return PrimitiveTopology(Topology::TriangleList); }
    static PrimitiveTopology TriangleStrip() { return PrimitiveTopology(Topology::TriangleStrip); }

    void SetTopology(Topology t) { topology_ = t; }
    Topology GetTopology() const { return topology_; }

    bool IsPoints() const { return topology_ == Topology::PointList; }
    bool IsLines() const {
        return topology_ == Topology::LineList || topology_ == Topology::LineStrip ||
               topology_ == Topology::LineListAdjacency;
    }
    bool IsTriangles() const {
        return topology_ == Topology::TriangleList || topology_ == Topology::TriangleStrip ||
               topology_ == Topology::TriangleFan || topology_ == Topology::TriangleListAdjacency;
    }
    bool IsStrip() const {
        return topology_ == Topology::LineStrip || topology_ == Topology::TriangleStrip ||
               topology_ == Topology::TriangleFan;
    }
    bool IsPatch() const { return topology_ == Topology::PatchList; }

    // Minimum vertices per primitive (approximate for adjacency). 0 = variable.
    uint32_t MinVertices() const {
        switch (topology_) {
            case Topology::PointList: return 1;
            case Topology::LineList: return 2;
            case Topology::LineStrip: return 2;
            case Topology::TriangleList: return 3;
            case Topology::TriangleStrip: return 3;
            case Topology::TriangleFan: return 3;
            case Topology::LineListAdjacency: return 4;
            case Topology::TriangleListAdjacency: return 6;
            case Topology::PatchList: return 0;
        }
        return 0;
    }

private:
    Topology topology_ = Topology::TriangleList;
};

} // namespace bighero
