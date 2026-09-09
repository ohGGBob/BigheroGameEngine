#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// ReflectionProbe: a cubemap reflection capture descriptor — world position,
// capture resolution, culling distance, and influence radius. Pure config.
class ReflectionProbe {
public:
    enum class Mode { Static, Realtime };

    ReflectionProbe() {}
    explicit ReflectionProbe(Mode mode) : mode_(mode) {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }

    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }
    void SetCaptureResolution(int s) { res_ = s < 1 ? 1 : s; }
    int CaptureResolution() const { return res_; }
    void SetCullDistance(float d) { cullDistance_ = d < 0 ? 0 : d; }
    float CullDistance() const { return cullDistance_; }

    void SetMode(Mode m) { mode_ = m; }
    Mode CurrentMode() const { return mode_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }

    void SetCubemap(std::uint64_t id) { cubeId_ = id; }
    std::uint64_t Cubemap() const { return cubeId_; }

    // Importance: influence relative to radius (used for blending probes).
    float ImportanceAt(float distance) const {
        if (radius_ <= 0) return 0;
        float t = 1.0f - (distance / radius_);
        return t < 0 ? 0 : (t > 1 ? 1 : t);
    }

private:
    Mode mode_ = Mode::Static;
    float px_=0, py_=0, pz_=0;
    float radius_ = 10.0f;
    int res_ = 256;
    float cullDistance_ = 500.0f;
    bool enabled_ = true;
    float intensity_ = 1.0f;
    std::uint64_t cubeId_ = 0;
};

} // namespace bighero
