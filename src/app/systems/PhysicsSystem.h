#pragma once
// 物理系统：ReactPhysics3D 封装 + 角色控制器 + 关节系统 + 射线检测
// 原 Application 中 physicsEngine_, physicsBodyIds_, characterBodyId_ 等逻辑

#include "ISubSystem.h"
#include "physics/PhysicsEngine.h"
#include "physics/PhysicsTypes.h"
#include <vector>

namespace BigHero::App
{

class PhysicsSystem final : public ISubSystem
{
public:
    PhysicsSystem() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "PhysicsSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -30; }

    void Init() { physicsEngine_.Init(); physicsEngine_.SetGravity(glm::vec3(0.0f, -9.81f, 0.0f)); }
    void Shutdown() override { physicsEngine_.Release(); physicsBodyIds_.clear(); physicsJointIds_.clear(); }

    void Update(const FrameContext& frame) override { if (physicsEnabled_) { physicsEngine_.Step(frame.deltaTime); SyncPhysicsBodies(); } }
    void PreRender(uint32_t) override {}
    void OnSwapchainRecreated() override {}
    void OnRenderPassRecreated() override {}

    void SyncPhysicsBodies() { /* 从物理引擎获取刚体变换，更新场景物体变换 */ }

    void RebuildPhysicsBodies() { physicsBodyIds_.clear(); for (const auto& obj : sceneRef_) { uint32_t id = physicsEngine_.CreateDynamicBox(obj.position, obj.scale); physicsBodyIds_.push_back(id); } }

    void InitCharacterController(const glm::vec3& spawnPos) { characterBodyId_ = physicsEngine_.CreateCharacterController(spawnPos); characterEnabled_ = true; }
    void UpdateCharacterController(float dt, const OrbitCamera& camera) { if (!characterEnabled_) return; /* WASD 移动、空格跳跃、地面检测、相机跟随 */ }

    [[nodiscard]] int Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const { return physicsEngine_.Raycast(origin, dir, maxDist); }

    void CreateJoint(uint32_t bodyA, uint32_t bodyB, Physics::JointType type) { uint32_t jointId = physicsEngine_.CreateJoint(bodyA, bodyB, type); physicsJointIds_.push_back(jointId); }

    void SetGravity(const glm::vec3& g) { physicsEngine_.SetGravity(g); gravity_ = g; }
    void SetPhysicsEnabled(bool enabled) { physicsEnabled_ = enabled; }
    void SetDebugDraw(bool enabled) { physicsDebugDraw_ = enabled; }
    void SetCharacterEnabled(bool enabled) { characterEnabled_ = enabled; }

    [[nodiscard]] bool PhysicsEnabled() const noexcept { return physicsEnabled_; }
    [[nodiscard]] bool DebugDrawEnabled() const noexcept { return physicsDebugDraw_; }
    [[nodiscard]] bool CharacterEnabled() const noexcept { return characterEnabled_; }
    [[nodiscard]] const std::vector<uint32_t>& BodyIds() const noexcept { return physicsBodyIds_; }
    [[nodiscard]] const std::vector<uint32_t>& JointIds() const noexcept { return physicsJointIds_; }
    [[nodiscard]] uint32_t CharacterBodyId() const noexcept { return characterBodyId_; }
    [[nodiscard]] Physics::PhysicsEngine& Engine() noexcept { return physicsEngine_; }

    void SetSceneRef(const std::vector<Scene::SceneObject>& scene) { sceneRef_ = scene; }

    [[nodiscard]] const char* Name() const noexcept override { return "PhysicsSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -30; }

private:
    Physics::PhysicsEngine physicsEngine_;
    std::vector<uint32_t> physicsBodyIds_;
    std::vector<uint32_t> physicsJointIds_;
    uint32_t characterBodyId_ = UINT32_MAX;
    bool physicsEnabled_ = true;
    bool physicsDebugDraw_ = false;
    bool characterEnabled_ = false;
    glm::vec3 gravity_{0.0f, -9.81f, 0.0f};
    std::vector<Scene::SceneObject> sceneRef_;
};

} // namespace BigHero::App