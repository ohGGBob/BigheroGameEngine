#pragma once
#include "PhysicsTypes.h"
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

namespace reactphysics3d
{
class PhysicsCommon;
class PhysicsWorld;
class RigidBody;
class CollisionShape;
} // namespace reactphysics3d

namespace BigHero::Physics
{
// 物理引擎封装：管理 ReactPhysics3D 的 PhysicsCommon / PhysicsWorld / 刚体集合。
// 提供与场景系统对接的简洁 API：创建/销毁刚体、读写变换、步进、调试线框。
// 所有刚体用连续整数 ID 管理，场景物体通过 ID 关联物理体。
class PhysicsEngine
{
  public:
    PhysicsEngine();
    ~PhysicsEngine();

    PhysicsEngine(const PhysicsEngine&) = delete;
    PhysicsEngine& operator=(const PhysicsEngine&) = delete;

    // 初始化物理世界（重力默认 -9.81）
    void Init();
    // 销毁所有刚体与物理世界
    void Destroy();

    // 步进物理模拟（固定步长推荐 1/60，内部可分子步）
    void Step(float deltaTime);

    // 创建刚体，返回 ID；type=None 时返回 UINT32_MAX 且不创建
    uint32_t CreateBody(const BodyConfig& config, const glm::vec3& position, const glm::quat& rotation);
    // 移除刚体（ID 失效）
    void RemoveBody(uint32_t id);
    // 移除所有刚体
    void RemoveAllBodies();

    // 设置刚体变换（用于运动学/静态体同步，动态体应避免每帧直接设置）
    void SetBodyTransform(uint32_t id, const glm::vec3& position, const glm::quat& rotation);
    // 读取刚体变换（动态体步进后同步回场景）
    void GetBodyTransform(uint32_t id, glm::vec3& outPosition, glm::quat& outRotation) const;

    // 设置重力
    void SetGravity(const glm::vec3& gravity);

    // 获取当前所有碰撞体的调试线框（每帧调用，用于编辑器可视化）
    [[nodiscard]] std::vector<DebugLine> GetDebugLines() const;

    // ---- 角色控制器辅助 ----
    // 设置刚体线速度（角色移动用，直接覆盖物理速度）
    void SetBodyLinearVelocity(uint32_t id, const glm::vec3& velocity);
    // 获取刚体线速度
    [[nodiscard]] glm::vec3 GetBodyLinearVelocity(uint32_t id) const;
    // 设置刚体角速度（角色控制器每帧清零防止倒下）
    void SetBodyAngularVelocity(uint32_t id, const glm::vec3& velocity);

    // 射线检测：从 origin 沿 direction 发射最大距离 maxDistance 的射线，返回最近命中
    [[nodiscard]] RaycastHit Raycast(const glm::vec3& origin, const glm::vec3& direction,
                                     float maxDistance = 100.0f) const;

    // 刚体数量
    [[nodiscard]] uint32_t BodyCount() const noexcept { return static_cast<uint32_t>(bodies_.size()); }

  private:
    reactphysics3d::PhysicsCommon* common_ = nullptr;
    reactphysics3d::PhysicsWorld* world_ = nullptr;
    std::vector<reactphysics3d::RigidBody*> bodies_;
    std::vector<BodyConfig> configs_;
    std::vector<bool> active_;
    std::vector<uint32_t> userTags_;
};
} // namespace BigHero::Physics
