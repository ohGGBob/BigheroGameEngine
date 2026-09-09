#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace bighero {

// ReflectionProbeConfig: settings for a reflection probe — capture resolution,
// blend distance, and culling mask. Pure config the reflection renderer reads.
class ReflectionProbeConfig {
public:
    enum class Type { Cubemap, Planar, Sphere };

    ReflectionProbeConfig() {}
    explicit ReflectionProbeConfig(std::uint64_t id) : id_(id) {}

    void SetId(std::uint64_t id) { id_ = id; }
    std::uint64_t Id() const { return id_; }
    void SetType(Type t) { type_ = t; }
    Type CurrentType() const { return type_; }
    void SetResolution(int r) { resolution_ = r < 16 ? 16 : (r > 2048 ? 2048 : r); }
    int Resolution() const { return resolution_; }
    void SetNearPlane(float n) { near_ = n < 0.01f ? 0.01f : n; }
    float NearPlane() const { return near_; }
    void SetFarPlane(float f) { far_ = f < near_ ? near_ : f; }
    float FarPlane() const { return far_; }
    void SetBlendDistance(float d) { blend_ = d < 0 ? 0 : d; }
    float BlendDistance() const { return blend_; }
    void SetCullingMask(std::uint32_t m) { cullingMask_ = m; }
    std::uint32_t CullingMask() const { return cullingMask_; }
    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    bool NeedRefresh() const { return needsRefresh_; }
    void MarkDirty() { needsRefresh_ = true; }
    void MarkClean() { needsRefresh_ = false; }

private:
    std::uint64_t id_ = 0;
    Type type_ = Type::Cubemap;
    int resolution_ = 256;
    float near_ = 0.1f, far_ = 100.0f, blend_ = 1.0f;
    std::uint32_t cullingMask_ = 0xFFFFFFFFu;
    float intensity_ = 1.0f;
    bool enabled_ = true, needsRefresh_ = true;
};

} // namespace bighero
