#pragma once
#include <cmath>

namespace bighero {

// Camera follow logic: smoothly (critically damped) track a target position
// with optional lookahead and dead-zone. Building block for 2D/3D cameras.
class CameraFollow {
public:
    enum class Mode { Snap, Smooth, LookAhead };

    explicit CameraFollow(Mode mode = Mode::Smooth) : mode_(mode) {}

    void SetMode(Mode m) { mode_ = m; }
    Mode FollowMode() const { return mode_; }

    // Current camera center.
    void SetPosition(float x, float y) { x_ = x; y_ = y; }
    void Position(float& x, float& y) const { x = x_; y = y_; }

    // Target to follow.
    void SetTarget(float x, float y) { tx_ = x; ty_ = y; }
    void Target(float& x, float& y) const { x = tx_; y = ty_; }

    // Smoothing amount (0..1 per update, higher = snappier).
    void SetSmooth(float s) { smooth_ = s < 0 ? 0 : (s > 1 ? 1 : s); }
    float Smooth() const { return smooth_; }

    // Lookahead distance (applied in LookAhead mode toward target velocity).
    void SetLookahead(float v) { lookahead_ = v; }
    float Lookahead() const { return lookahead_; }

    // Dead-zone size: camera does not move until target exits this box.
    void SetDeadZone(float w, float h) { dzW_ = w; dzH_ = h; }

    void SetVelocity(float vx, float vy) { vx_ = vx; vy_ = vy; }

    // Advance the camera by dt and update internal state.
    void Update(float dt) {
        if (mode_ == Mode::Snap) {
            x_ = tx_ + vx_ * lookahead_;
            y_ = ty_ + vy_ * lookahead_;
            return;
        }
        // Dead-zone handling.
        float ox = x_, oy = y_;
        if (std::fabs(tx_ - x_) > dzW_ * 0.5f)
            ox = tx_ - std::copysign(dzW_ * 0.5f, tx_ - x_);
        if (std::fabs(ty_ - y_) > dzH_ * 0.5f)
            oy = ty_ - std::copysign(dzH_ * 0.5f, ty_ - y_);
        float gx = ox + vx_ * lookahead_;
        float gy = oy + vy_ * lookahead_;
        // Exponential smoothing, frame-rate independent.
        float k = 1.0f - std::pow(1.0f - smooth_, std::max(1.0f, dt * 60.0f));
        x_ += (gx - x_) * k;
        y_ += (gy - y_) * k;
    }

private:
    Mode mode_ = Mode::Smooth;
    float x_ = 0, y_ = 0;
    float tx_ = 0, ty_ = 0;
    float vx_ = 0, vy_ = 0;
    float smooth_ = 0.15f;
    float lookahead_ = 0.0f;
    float dzW_ = 0.0f, dzH_ = 0.0f;
};

} // namespace bighero
