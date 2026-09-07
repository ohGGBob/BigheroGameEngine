#pragma once
#include <cstdint>
#include <string>
#include <cstddef>

namespace bighero {

// Renderer component for a mesh: holds geometry/material handles and draw
// state. A pure data holder used by the render systems.
class MeshRenderer {
public:
    MeshRenderer() {}
    MeshRenderer(uint64_t mesh, int material, int shader)
        : mesh_(mesh), material_(material), shader_(shader) {}

    void SetMesh(uint64_t m) { mesh_ = m; }
    uint64_t Mesh() const { return mesh_; }
    void SetMaterial(int m) { material_ = m; }
    int Material() const { return material_; }
    void SetShader(int s) { shader_ = s; }
    int Shader() const { return shader_; }

    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const { return visible_; }
    void SetLayer(int l) { layer_ = l; }
    int Layer() const { return layer_; }
    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

    bool IsValid() const { return mesh_ != 0; }

private:
    uint64_t mesh_ = 0;
    int material_ = 0;
    int shader_ = 0;
    bool visible_ = true;
    int layer_ = 0;
    std::string name_;
};

} // namespace bighero
