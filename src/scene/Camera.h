#pragma once
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace BigHero
{
// 轨道相机：绕目标点球面运动，鼠标拖拽旋转、滚轮缩放
class OrbitCamera
{
  public:
    // 拖拽旋转：dx/dy为光标位移（像素）
    void Orbit(float dx, float dy)
    {
        yaw_ -= dx * orbitSpeed_;
        pitch_ -= dy * orbitSpeed_;
        // pitch限制在(-6°, 88°)，避免钻入地面以下或越过顶点翻转
        pitch_ = std::clamp(pitch_, -0.1f, 1.53f);
    }

    // 滚轮缩放：delta向上为正
    void Zoom(double delta)
    {
        distance_ = std::clamp(distance_ * static_cast<float>(std::pow(0.9, delta)), minDistance_, maxDistance_);
    }

    // 平移目标点：forward为视线水平朝向（深入屏幕），right为屏幕右向，up为世界竖直
    void Pan(float forward, float right, float up)
    {
        const glm::vec3 forwardDir(-std::sin(yaw_), 0.0f, -std::cos(yaw_));
        const glm::vec3 rightDir(std::cos(yaw_), 0.0f, -std::sin(yaw_));
        target_ += forwardDir * forward + rightDir * right;
        target_.y = std::clamp(target_.y + up, -2.0f, 20.0f);
        target_.x = std::clamp(target_.x, -15.0f, 15.0f);
        target_.z = std::clamp(target_.z, -15.0f, 15.0f);
    }

    // 依据当前参数刷新view/proj；aspect取自交换链宽高比
    void Update(float aspect)
    {
        const float cosPitch = std::cos(pitch_);
        position_ =
            target_ + distance_ * glm::vec3(cosPitch * std::sin(yaw_), std::sin(pitch_), cosPitch * std::cos(yaw_));

        view_ = glm::lookAt(position_, target_, glm::vec3(0.0f, 1.0f, 0.0f));
        proj_ = glm::perspective(glm::radians(fovDegrees_), aspect, nearZ_, farZ_);
        proj_[1][1] *= -1.0f; // Vulkan NDC的Y轴朝下，翻转投影
    }

    [[nodiscard]] const glm::mat4& View() const noexcept { return view_; }
    [[nodiscard]] const glm::mat4& Proj() const noexcept { return proj_; }
    [[nodiscard]] const glm::vec3& Position() const noexcept { return position_; }
    [[nodiscard]] const glm::vec3& Target() const noexcept { return target_; }
    [[nodiscard]] float Yaw() const noexcept { return yaw_; }

    // 设置相机注视点（第三人称跟随用，相机保持当前距离/角度绕新目标旋转）
    void SetTarget(const glm::vec3& target) noexcept { target_ = target; }

    float fovDegrees_ = 60.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 500.0f;

  private:
    glm::mat4 view_{1.0f};
    glm::mat4 proj_{1.0f};
    glm::vec3 target_{0.0f, 0.5f, 0.0f};
    glm::vec3 position_{0.0f, 3.0f, 6.0f};
    float yaw_ = 0.8f;
    float pitch_ = 0.45f;
    float distance_ = 7.0f;
    float minDistance_ = 1.5f;
    float maxDistance_ = 40.0f;
    float orbitSpeed_ = 0.0045f;
};
} // namespace BigHero
