#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Touch: represents a single touch contact (id, position, phase) with velocity
// and pressure. Self-contained, std-lib only.
class Touch {
public:
    enum class Phase : int {
        None = 0, Began = 1, Moved = 2, Stationary = 3, Ended = 4, Canceled = 5
    };

    Touch() = default;

    void SetId(int id) { id_ = id; }
    int Id() const { return id_; }
    void ClearId() { id_ = -1; }

    void SetPosition(float x, float y) { x_=x; y_=y; }
    float X() const { return x_; }
    float Y() const { return y_; }

    void SetDelta(float dx, float dy) { dx_=dx; dy_=dy; }
    float DeltaX() const { return dx_; }
    float DeltaY() const { return dy_; }

    void SetPressure(float p) { pressure_ = p < 0 ? 0 : (p > 1 ? 1 : p); }
    float Pressure() const { return pressure_; }

    void SetPhase(Phase p) { phase_ = p; }
    Phase GetPhase() const { return phase_; }
    bool IsActive() const { return phase_ == Phase::Began || phase_ == Phase::Moved || phase_ == Phase::Stationary; }
    bool IsEnded() const { return phase_ == Phase::Ended || phase_ == Phase::Canceled; }

    // Velocity in units/second, derived from delta over dt seconds.
    float VelocityX(float dt) const { return dt > 0 ? dx_/dt : 0; }
    float VelocityY(float dt) const { return dt > 0 ? dy_/dt : 0; }
    float Magnitude() const { return std::sqrt(x_*x_ + y_*y_); }
    float DeltaMagnitude() const { return std::sqrt(dx_*dx_ + dy_*dy_); }

    void EndFrame() { dx_=0; dy_=0; }

private:
    int id_ = -1;
    float x_=0, y_=0, dx_=0, dy_=0, pressure_=0;
    Phase phase_ = Phase::None;
};

} // namespace bighero
