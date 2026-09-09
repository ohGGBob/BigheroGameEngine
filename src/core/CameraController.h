#pragma once
#include <cmath>

namespace bighero {

// Camera controller: WASD/mouse orbit + zoom for a free camera. Pure
// CPU-side input-driven movement; backend sets the final view matrix.
class CameraController {
public:
    enum class Mode { Fly, Orbit };

    CameraController() {}
    explicit CameraController(Mode mode) : mode_(mode) {}

    void SetMode(Mode m) { mode_ = m; }
    Mode CameraMode() const { return mode_; }

    void SetPosition(float x, float y, float z) { x_ = x; y_ = y; z_ = z; }
    void Position(float& x, float& y, float& z) const { x = x_; y = y_; z = z_; }

    void SetYawPitch(float yawDeg, float pitchDeg) { yaw_ = yawDeg; pitch_ = pitchDeg; }
    void YawPitch(float& yawDeg, float& pitchDeg) const { yawDeg = yaw_; pitchDeg = pitch_; }

    void SetMoveSpeed(float s) { moveSpeed_ = s < 0 ? 0 : s; }
    float MoveSpeed() const { return moveSpeed_; }
    void SetOrbitSpeed(float s) { orbitSpeed_ = s; }
    float OrbitSpeed() const { return orbitSpeed_; }
    void SetZoomSpeed(float s) { zoomSpeed_ = s; }
    float ZoomSpeed() const { return zoomSpeed_; }

    // Apply movement from key/mouse deltas.
    void Move(float forward, float right, float up) {
        float s = moveSpeed_;
        // Forward along -Z (screen depth) for Fly, horizontal plane for other.
        if (mode_ == Mode::Fly) {
            x_ += right * s;
            z_ -= forward * s;
            y_ += up * s;
        } else {
            x_ += right * s;
            z_ -= forward * s;
        }
    }
    void Rotate(float yawDeltaDeg, float pitchDeltaDeg) {
        yaw_ += yawDeltaDeg * orbitSpeed_;
        pitch_ += pitchDeltaDeg * orbitSpeed_;
        if (pitch_ > 89) pitch_ = 89;
        if (pitch_ < -89) pitch_ = -89;
    }
    void Zoom(float wheelDelta) { dist_ -= wheelDelta * zoomSpeed_; if (dist_ < 0.1f) dist_ = 0.1f; }

    void SetDistance(float d) { dist_ = d < 0.1f ? 0.1f : d; }
    float Distance() const { return dist_; }

private:
    Mode mode_ = Mode::Fly;
    float x_ = 0, y_ = 0, z_ = 0;
    float yaw_ = 0, pitch_ = 0;
    float dist_ = 10.0f;
    float moveSpeed_ = 10.0f;
    float orbitSpeed_ = 1.0f;
    float zoomSpeed_ = 1.0f;
};

} // namespace bighero
