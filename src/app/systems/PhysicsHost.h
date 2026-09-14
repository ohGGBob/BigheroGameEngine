#pragma once
// 阶段 3f：物理子系统（自 Application 拆出，阶段 3 中耦合最深的一块）。
// 职责：刚体/关节生命周期（ECS PhysicsBody 组件 → 物理世界的全量重建与逐帧同步）、
//       引擎步进与动态体变换回写 ECS、第三人称角色控制器（胶囊刚体 + WASD/空格）。
// 构造注入 ECS 场景 / 投影包 / 相机 / 窗口引用；编辑器开关经公有字段直写。
// 纯 CPU 逻辑（无 GPU 资源）；不含射线拾取与调试线可视化（留在 Application，
// 经 engine/debugDraw 公有成员消费）。

#include "physics/PhysicsEngine.h"
#include "platform/Window.h"
#include "scene/Camera.h"
#include "scene/EcsScene.h"
#include "scene/Scene.h"

#include <glm/glm.hpp>
#include <vector>

namespace BigHero
{
class PhysicsHost
{
  public:
    PhysicsHost(Scene::EcsScene& ecsScene, std::vector<Scene::SceneObject>& scene, OrbitCamera& camera, Window& window)
        : ecsScene_(ecsScene), scene_(scene), camera_(camera), window_(window)
    {
    }

    PhysicsHost(const PhysicsHost&) = delete;
    PhysicsHost& operator=(const PhysicsHost&) = delete;

    // ---- 引擎与状态（编辑器经指针直写） ----
    Physics::PhysicsEngine engine;
    bool enabled = true;    // 物理模拟总开关
    bool debugDraw = false; // 编辑器物理调试线框
    float gravity = -9.81f; // 世界重力 Y（编辑器可调）

    // ---- 场景关节 ----
    std::vector<Physics::SceneJoint> joints; // 场景关节（编辑器可增删，变更后 RebuildBodies）
    std::vector<uint32_t> jointIds;          // 每个场景关节对应的物理关节 ID

    // ---- 角色控制器 ----
    uint32_t characterBodyId = UINT32_MAX;
    bool characterEnabled = false;
    bool prevCharacterEnabled = false;
    float characterSpeed = 6.0f;     // 移动速度（m/s）
    float characterJumpForce = 7.5f; // 跳跃初速度（m/s）
    bool characterGrounded = false;
    glm::vec3 characterSpawn{0.0f, 2.0f, 0.0f};

    // 初始化：引擎启动 + 应用默认重力
    void Init();

    // 全量重建刚体/关节：地面 + ECS PhysicsBody 实体 + 角色胶囊 + 场景关节
    // （编辑器物理属性变更 / 场景增删 / 加载 / 快照还原后调用）
    void RebuildBodies();

    // 每帧推进：角色开关边沿 → 运动学/静态体同步 → 角色速度 → 步进 →
    //          动态体回写 ECS Transform + 角色跟随相机/掉落重生
    void Update(float dt);

  private:
    // 运动学/静态体：ECS 变换组件同步到物理
    void SyncBodies();

    // 步进前设置角色水平速度/跳跃（基于相机 yaw 的移动方向）
    void UpdateCharacter();

    Scene::EcsScene& ecsScene_;
    std::vector<Scene::SceneObject>& scene_;
    OrbitCamera& camera_;
    Window& window_;
};
} // namespace BigHero
