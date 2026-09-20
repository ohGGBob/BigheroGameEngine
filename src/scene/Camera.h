#pragma once
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace BigHero
{
// 轨道相机：绕目标点球面运动，鼠标拖拽旋转、滚轮缩放。
// 眼点受 minEyeY 穿地解析约束（UPGRADE_PLAN P1-11 / 第二轮评估 D10 治本：
// 防钻地不再靠收窄 pitch 输入域，而由 Update/ComputePosition 统一收口的高度约束保证）。
class OrbitCamera
{
  public:
    // pitch 输入域（弧度）：下限恢复 -1.4（约-80°，可俯视地平线以下观察物体底部），
    // 上限保持 1.53（约88°，接近天顶但不翻转 lookAt 的 up 向量）
    static constexpr float kPitchMin = -1.4f;
    static constexpr float kPitchMax = 1.53f;

    // 拖拽旋转：dx/dy为光标位移（像素）
    void Orbit(float dx, float dy)
    {
        yaw_ -= dx * orbitSpeed_;
        pitch_ -= dy * orbitSpeed_;
        // 输入域限制（穿地防护由 minEyeY 约束接管，见 ConstrainedPitch/ConstrainedEyePosition）
        pitch_ = std::clamp(pitch_, kPitchMin, kPitchMax);
    }

    // 滚轮缩放：delta向上为正
    void Zoom(double delta)
    {
        distance_ = std::clamp(distance_ * static_cast<float>(std::pow(0.9, delta)), minDistance_, maxDistance_);
    }

    // 动态限制相机距离下限（防止穿入大型物体内部导致背面剔除全黑）
    void ClampDistance(float minDist) noexcept { distance_ = std::max(distance_, minDist); }

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
        // 穿地约束统一收口：Orbit/Zoom/Pan/SetTarget/SetDistance 等改参路径均在本处生效
        position_ = ConstrainedEyePosition();
        pitch_ = ConstrainedPitch(); // 写回约束后的 pitch：状态与渲染一致，越界拖拽回拉无需先退越界量

        view_ = glm::lookAt(position_, target_, glm::vec3(0.0f, 1.0f, 0.0f));
        proj_ = glm::perspective(glm::radians(fovDegrees_), aspect, nearZ_, farZ_);
        proj_[1][1] *= -1.0f; // Vulkan NDC的Y轴朝下，翻转投影
        // 升级 28：TAA 亚像素抖动——向 z→x/y 系数注入 NDC 偏移（渲染画面整体平移 |jitter| 个亚像素），
        // 与 pp_taa.frag 的 "+jitter 还原未抖动 NDC" 互逆，供时间累积去锯齿
        proj_[2][0] += jitterNdc_.x;
        proj_[2][1] += jitterNdc_.y;
    }

    // 按当前参数（不含 TAA 抖动）计算相机世界位置（含穿地约束，与 Update 的渲染位置一致），不修改内部状态
    [[nodiscard]] glm::vec3 ComputePosition() const noexcept { return ConstrainedEyePosition(); }

    // 升级 28：设置当前帧 NDC 抖动量（[-1,1]），需在 Update() 之前调用；关闭 TAA 时置零
    void SetJitter(float ndcX, float ndcY) noexcept { jitterNdc_ = glm::vec2(ndcX, ndcY); }

    [[nodiscard]] const glm::mat4& View() const noexcept { return view_; }
    [[nodiscard]] const glm::mat4& Proj() const noexcept { return proj_; }
    [[nodiscard]] const glm::vec3& Position() const noexcept { return position_; }
    [[nodiscard]] const glm::vec3& Target() const noexcept { return target_; }
    [[nodiscard]] float Yaw() const noexcept { return yaw_; }
    [[nodiscard]] float Pitch() const noexcept { return pitch_; }

    // 双模式切换同步：外部写入 yaw/pitch/distance（第一人称↔轨道相机互切时保持视觉连续性）
    void SetYaw(float yaw) noexcept { yaw_ = yaw; }
    void SetPitch(float pitch) noexcept { pitch_ = std::clamp(pitch, kPitchMin, kPitchMax); }
    void SetDistance(float dist) noexcept { distance_ = std::clamp(dist, minDistance_, maxDistance_); }

    // 穿地约束参数（P1-11/D10）：眼点高度下限。OrbitCamera 不感知场景，默认覆盖默认场景地面
    // （地面 y=0，留 0.05 余量防贴地穿模/深度冲突）；场景地面高度不同时由调用方传入。
    void SetMinEyeY(float minEyeY) noexcept { minEyeY_ = minEyeY; }
    [[nodiscard]] float GetMinEyeY() const noexcept { return minEyeY_; }

    // 设置相机注视点（第三人称跟随用，相机保持当前距离/角度绕新目标旋转）
    void SetTarget(const glm::vec3& target) noexcept { target_ = target; }

    [[nodiscard]] float GetDistance() const noexcept { return distance_; }

    float fovDegrees_ = 60.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 500.0f;

  private:
    // 穿地解析约束（P1-11/D10 治本）：eye.y = target_.y + distance_·sin(pitch) ≥ minEyeY_
    // ⇔ sin(pitch) ≥ (minEyeY_ - target_.y)/distance_。纯逻辑，不依赖场景查询：
    //   1) 常规情形（目标点不低于 minEyeY_）：asin 恒有解 → 抬升 pitch 至下界，
    //      保持取景距离与球面轨道语义不变；
    //   2) 退化情形（目标点低于 minEyeY_ 且 distance_ 不足以越过地面）：pitch 抬至上限仍越界，
    //      由 ConstrainedEyePosition 沿视线方向推出距离兜底（视线朝向不变）。
    // 约束未触发时与旧球面公式（c1433f5 收窄输入域之前）逐位一致。
    [[nodiscard]] float ConstrainedPitch() const noexcept
    {
        const float d = std::max(distance_, 1e-6f);
        const float sinFloor = (minEyeY_ - target_.y) / d;
        const float pitchFloor = std::asin(std::clamp(sinFloor, -1.0f, 1.0f));
        const float p = std::max(std::clamp(pitch_, kPitchMin, kPitchMax), pitchFloor);
        return std::min(p, kPitchMax);
    }

    [[nodiscard]] glm::vec3 ConstrainedEyePosition() const noexcept
    {
        const float p = ConstrainedPitch();
        const float d = std::max(distance_, 1e-6f);
        const glm::vec3 dir(std::cos(p) * std::sin(yaw_), std::sin(p), std::cos(p) * std::cos(yaw_));
        glm::vec3 eye = target_ + d * dir;
        if (eye.y < minEyeY_) // 仅退化分支可达：此时 sin(p) > 0，推出距离后眼点精确贴合 minEyeY_
            eye = target_ + ((minEyeY_ - target_.y) / std::sin(p)) * dir;
        // 浮点舍入防护：两处解析解都可能因舍入低于下限 1-2 ulp，硬性贴齐保证 "eye.y ≥ minEyeY" 契约
        eye.y = std::max(eye.y, minEyeY_);
        return eye;
    }

    glm::mat4 view_{1.0f};
    glm::mat4 proj_{1.0f};
    glm::vec3 target_{0.0f, 0.5f, 0.0f};
    glm::vec3 position_{0.0f, 3.0f, 6.0f};
    float yaw_ = 0.8f;
    float pitch_ = 0.45f;
    float distance_ = 7.0f;
    float minDistance_ = 1.5f;
    float maxDistance_ = 40.0f;
    float minEyeY_ = 0.05f; // 眼点高度下限（穿地约束，默认场景地面 y=0 + 0.05 余量）
    float orbitSpeed_ = 0.0045f;
    glm::vec2 jitterNdc_{0.0f}; // 升级 28：TAA 抖动量（NDC 空间）
};
} // namespace BigHero
