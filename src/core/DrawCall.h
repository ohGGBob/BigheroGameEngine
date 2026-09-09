#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// DrawCall: a single batchable draw command descriptor — mesh, material,
// vertex/instance count, and the render pass it belongs to. The backend
// groups consecutive draw calls into render batches. Pure data container.
class DrawCall {
public:
    DrawCall() {}
    DrawCall(std::uint64_t mesh, std::uint64_t material)
        : mesh_(mesh), material_(material) {}

    void SetMesh(std::uint64_t m) { mesh_ = m; }
    std::uint64_t Mesh() const { return mesh_; }
    void SetMaterial(std::uint64_t m) { material_ = m; }
    std::uint64_t Material() const { return material_; }

    void SetIndexCount(std::size_t n) { indexCount_ = n; }
    std::size_t IndexCount() const { return indexCount_; }
    void SetInstanceCount(std::size_t n) { instanceCount_ = n; }
    std::size_t InstanceCount() const { return instanceCount_; }

    void SetRenderPass(int pass) { pass_ = pass; }
    int RenderPass() const { return pass_; }
    void SetOrder(int order) { order_ = order; }
    int Order() const { return order_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Approximate primitive count for stats (base * instances).
    std::size_t EffectiveTriangles(std::size_t baseTriangles) const {
        return baseTriangles * instanceCount_;
    }

    bool IsValid() const { return mesh_ != 0; }

private:
    std::uint64_t mesh_ = 0;
    std::uint64_t material_ = 0;
    std::size_t indexCount_ = 0;
    std::size_t instanceCount_ = 1;
    int pass_ = 0;
    int order_ = 0;
    bool enabled_ = true;
};

} // namespace bighero
