#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// Describes a skybox render pass: a cubemap texture plus optional tint/rotation
// and render order. CPU-side metadata used by the sky system.
class SkyboxRenderer {
public:
    SkyboxRenderer() {}
    SkyboxRenderer(std::uint64_t cubemap, const std::string& name = "")
        : cubemap_(cubemap), name_(name) {}

    void SetCubemap(std::uint64_t cube) { cubemap_ = cube; }
    std::uint64_t Cubemap() const { return cubemap_; }
    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

    void SetTint(std::uint32_t tint) { tint_ = tint; }
    std::uint32_t Tint() const { return tint_; }
    void SetRotation(float yawDeg) { rotation_ = yawDeg; }
    float Rotation() const { return rotation_; }

    // Render layer / draw order (lower renders first).
    void SetOrder(int order) { order_ = order; }
    int Order() const { return order_; }
    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const { return visible_; }
    void SetEnvironmentReflection(bool enable) { reflection_ = enable; }
    bool EnvironmentReflection() const { return reflection_; }

    bool IsValid() const { return cubemap_ != 0; }
    bool HasCubemap() const { return cubemap_ != 0; }

private:
    std::uint64_t cubemap_ = 0;
    std::string name_;
    std::uint32_t tint_ = 0xFFFFFFFFu;
    float rotation_ = 0.0f;
    int order_ = -1000;   // skybox renders first by default
    bool visible_ = true;
    bool reflection_ = true;
};

} // namespace bighero
