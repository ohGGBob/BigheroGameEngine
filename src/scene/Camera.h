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
        // pitch限制在水平线以上，避免拉远时相机钻入地面以下导致全黑
        pitch_ = std::clamp(pitch_, 0.0f, 1.53f);
    }

    // 滚轮缩放：delta向上为正
    void Zoom(double delta)
    {
        distance_ = std::clamp(distance_ * static_cast<float>(std::pow(0.9, delta)), minDistance_, maxDistance_);
    }

    // 动态限制相机距离下限（防止穿入大型物体内部导致背面剔除全黑）
    void ClampDistance(float minDist) noexcept
    {
        distance_ = std::max(distance_, minDist);
    }

    [[nodiscard]] float GetMinDistance() const noexcept { return minDistance_; }

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
        // 升级 28：TAA 亚像素抖动——向 z→x/y 系数注入 NDC 偏移（渲染画面整体平移 |jitter| 个亚像素），
        // 与 pp_taa.frag 的 "+jitter 还原未抖动 NDC" 互逆，供时间累积去锯齿
        proj_[2][0] += jitterNdc_.x;
        proj_[2][1] += jitterNdc_.y;
    }

    // 按当前参数（不含 TAA 抖动）计算相机世界位置，不修改内部状态
    [[nodiscard]] glm::vec3 ComputePosition() const noexcept
    {
        const float cosPitch = std::cos(pitch_);
        return target_ + distance_ * glm::vec3(cosPitch * std::sin(yaw_), std::sin(pitch_), cosPitch * std::cos(yaw_));
    }

    // 升级 28：设置当前帧 NDC 抖动量（[-1,1]），需在 Update() 之前调用；关闭 TAA 时置零
    void SetJitter(float ndcX, float ndcY) noexcept { jitterNdc_ = glm::vec2(ndcX, ndcY); }

    [[nodiscard]] const glm::mat4& View() const noexcept { return view_; }
    [[nodiscard]] const glm::mat4& Proj() const noexcept { return proj_; }
    [[nodiscard]] const glm::vec3& Position() const noexcept { return position_; }
    [[nodiscard]] const glm::vec3& Target() const noexcept { return target_; }
    [[nodiscard]] float Yaw() const noexcept { return yaw_; }

    // 双模式切换同步：外部写入 yaw/pitch/distance（第一人称↔轨道相机互切时保持视觉连续性）
    void SetYaw(float yaw) noexcept { yaw_ = yaw; }
    void SetPitch(float pitch) noexcept { pitch_ = std::clamp(pitch, 0.0f, 1.53f); }
    void SetDistance(float dist) noexcept { distance_ = std::clamp(dist, minDistance_, maxDistance_); }

    // 设置相机注视点（第三人称跟随用，相机保持当前距离/角度绕新目标旋转）
    void SetTarget(const glm::vec3& target) noexcept { target_ = target; }

    [[nodiscard]] float GetDistance() const noexcept { return distance_; }

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
    glm::vec2 jitterNdc_{0.0f}; // 升级 28：TAA 抖动量（NDC 空间）
};
} // namespace BigHero
