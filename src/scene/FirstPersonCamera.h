#pragma once
// 第一人称漫游相机：沉浸式场景内部自由走动观察。
// 支持 WASD 水平移动（+空格/Shift 上升下降）、鼠标视角旋转。
// 与 OrbitCamera 保持同构接口（View/Proj/Position/Update），便于双模式切换。

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace BigHero
{
class FirstPersonCamera
{
  public:
    // 按输入方向（相机本地坐标系）移动，dt=帧时间。
    // forward/right/up 为归一化方向系数（通常 -1/0/1），speed 为 m/s。
    void Move(float forward, float right, float up, float dt, float speed = 3.0f)
    {
        const glm::vec3 fwd(std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_));
        const glm::vec3 dir = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z)); // 水平朝向
        const glm::vec3 rgt = glm::normalize(glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f)));
        position_ += (dir * forward + rgt * right) * speed * dt;
        position_.y += up * speed * dt;
        position_.y = std::clamp(position_.y, minHeight_, maxHeight_);
        // 水平边界（房间大致范围，soft clamp）
        position_.x = std::clamp(position_.x, -worldBounds_, worldBounds_);
        position_.z = std::clamp(position_.z, -worldBounds_, worldBounds_);
    }

    // 鼠标视角旋转：dx/dy 为光标位移（像素），灵敏度可调
    void Rotate(float dx, float dy, float sensitivity = 0.002f)
    {
        yaw_ -= dx * sensitivity;
        pitch_ -= dy * sensitivity;
        pitch_ = std::clamp(pitch_, -1.55f, 1.55f); // 允许仰视/俯视，接近垂直但不翻转
    }

    // 刷新 view/proj；aspect 取自交换链宽高比
    void Update(float aspect)
    {
        const float cosP = std::cos(pitch_);
        const glm::vec3 front(std::sin(yaw_) * cosP, std::sin(pitch_), std::cos(yaw_) * cosP);
        view_ = glm::lookAt(position_, position_ + front, glm::vec3(0.0f, 1.0f, 0.0f));
        proj_ = glm::perspective(glm::radians(fovDegrees_), aspect, nearZ_, farZ_);
        proj_[1][1] *= -1.0f; // Vulkan NDC Y 朝下
        // TAA 亚像素抖动（与 OrbitCamera 同构）：向 z→x/y 系数注入 NDC 偏移，
        // 与 pp_taa.frag 的 "+jitter 还原未抖动 NDC" 互逆，供时间累积去锯齿
        proj_[2][0] += jitterNdc_.x;
        proj_[2][1] += jitterNdc_.y;
    }

    // 设置当前帧 NDC 抖动量（[-1,1]），需在 Update() 之前调用；关闭 TAA 时置零
    void SetJitter(float ndcX, float ndcY) noexcept { jitterNdc_ = glm::vec2(ndcX, ndcY); }

    // 当前视线前向（世界空间，包含 pitch 俯仰）
    [[nodiscard]] glm::vec3 Forward() const noexcept
    {
        const float cosP = std::cos(pitch_);
        return glm::vec3(std::sin(yaw_) * cosP, std::sin(pitch_), std::cos(yaw_) * cosP);
    }

    // 同步到 OrbitCamera（双模式切换时保持视觉连续性：FP → Orbit 保留朝向与位置）
    void SyncToOrbit(class OrbitCamera& orbit) const;
    void SyncFromOrbit(const class OrbitCamera& orbit);

    [[nodiscard]] const glm::mat4& View() const noexcept { return view_; }
    [[nodiscard]] const glm::mat4& Proj() const noexcept { return proj_; }
    [[nodiscard]] const glm::vec3& Position() const noexcept { return position_; }
    [[nodiscard]] float Yaw() const noexcept { return yaw_; }
    [[nodiscard]] float Pitch() const noexcept { return pitch_; }

    void SetPosition(const glm::vec3& p) noexcept { position_ = p; }
    void SetYawPitch(float yaw, float pitch) noexcept
    {
        yaw_ = yaw;
        pitch_ = std::clamp(pitch, -1.55f, 1.55f);
    }

    // 渲染参数与 OrbitCamera 保持一致（后处理/物理等系统共用）
    float fovDegrees_ = 60.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 500.0f;

  private:
    glm::mat4 view_{1.0f};
    glm::mat4 proj_{1.0f};
    glm::vec3 position_{0.0f, 1.6f, 0.0f}; // 站立眼高
    float yaw_ = 0.8f;
    float pitch_ = 0.0f;
    float minHeight_ = 0.2f; // 蹲下最低
    float maxHeight_ = 3.0f; // 跳跃/飞行最高
    float worldBounds_ = 20.0f;
    glm::vec2 jitterNdc_{0.0f}; // TAA 抖动量（NDC 空间）
};
} // namespace BigHero
