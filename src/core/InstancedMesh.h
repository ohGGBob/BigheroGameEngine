#pragma once
#include <vector>
#include <cstddef>
#include "Material.h"

namespace bighero {

// InstancedMesh: a batch-friendly mesh that draws N instances of a geometry
// with per-instance transforms and a shared material. Holds the base mesh
// handle, instance count, and per-instance model matrices (flat 4x4). Pure data.
class InstancedMesh {
public:
    InstancedMesh() {}
    InstancedMesh(std::uint64_t mesh, std::size_t instances)
        : mesh_(mesh) { Resize(instances); }

    void SetMesh(std::uint64_t m) { mesh_ = m; }
    std::uint64_t Mesh() const { return mesh_; }

    void Resize(std::size_t n) { matrices_.assign(n * 16, 0.0f); }
    std::size_t InstanceCount() const { return matrices_.size() / 16; }

    // Set instance i's model matrix from 16 floats (row-major).
    void SetMatrix(std::size_t i, const float m[16]) {
        if ((i + 1) * 16 > matrices_.size()) return;
        for (int c = 0; c < 16; ++c) matrices_[i * 16 + c] = m[c];
    }
    bool GetMatrix(std::size_t i, float m[16]) const {
        if ((i + 1) * 16 > matrices_.size()) return false;
        for (int c = 0; c < 16; ++c) m[c] = matrices_[i * 16 + c];
        return true;
    }

    void SetMaterial(const Material& mat) { material_ = mat; }
    const Material& GetMaterial() const { return material_; }

    void SetShadowCasting(bool s) { castShadow_ = s; }
    bool ShadowCasting() const { return castShadow_; }

    std::size_t VertexCapacity() const { return InstanceCount() * 24; } // 24 = 8 verts * 3 (approx)

private:
    std::uint64_t mesh_ = 0;
    std::vector<float> matrices_;
    Material material_;
    bool castShadow_ = true;
};

} // namespace bighero
