#include "scene/FirstPersonCamera.h"
#include "scene/Camera.h"

namespace BigHero
{
// FP → Orbit：保留位置为注视点，distance=3，朝向反向推导 yaw/pitch
void FirstPersonCamera::SyncToOrbit(OrbitCamera& orbit) const
{
    orbit.SetTarget(position_);
    orbit.SetYaw(yaw_);
    orbit.SetPitch(pitch_);
    orbit.SetDistance(3.0f);
}

// Orbit → FP：视线起点与方向与轨道相机完全一致（无缝接管视角）。
// 由 target - position 反推 yaw/pitch：FP forward=(sin(yaw)cosP, sinP, cos(yaw)cosP)，
// 故 yaw = atan2(dx, dz)，pitch = asin(dy)（方向与 Orbit 看向目标完全一致）。
void FirstPersonCamera::SyncFromOrbit(const OrbitCamera& orbit)
{
    position_ = orbit.Position();
    const glm::vec3 dir = glm::normalize(orbit.Target() - orbit.Position());
    yaw_ = std::atan2(dir.x, dir.z);
    pitch_ = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
}
} // namespace BigHero
