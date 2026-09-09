#pragma once
#include <cmath>

namespace bighero {

// Camera: a thin, self-contained camera descriptor storing projection type,
// field-of-view, near/far clips, and viewport. Does not depend on GPU state;
// the renderer consumes it to build view/projection matrices.
struct Camera {
    enum class Projection { Perspective, Orthographic };

    Projection projection = Projection::Perspective;
    float fovYDeg = 60.0f;      // vertical FOV in degrees (perspective)
    float orthoSize = 10.0f;    // half height in world units (orthographic)
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    float aspect = 16.0f / 9.0f;
    // Viewport in normalized [0,1] screen space.
    float vpX = 0, vpY = 0, vpW = 1, vpH = 1;

    Camera() = default;

    void SetPerspective(float fovYDeg_, float aspect_, float near_, float far_) {
        projection = Projection::Perspective;
        fovYDeg = fovYDeg_; aspect = aspect_;
        nearClip = near_; farClip = far_;
    }
    void SetOrthographic(float size_, float aspect_, float near_, float far_) {
        projection = Projection::Orthographic;
        orthoSize = size_; aspect = aspect_;
        nearClip = near_; farClip = far_;
    }
    // Vertical FOV in radians.
    float FovYRad() const { return fovYDeg * 3.14159265f / 180.0f; }
    float ViewportWidth() const { return vpW; }
    float ViewportHeight() const { return vpH; }
};

} // namespace bighero
