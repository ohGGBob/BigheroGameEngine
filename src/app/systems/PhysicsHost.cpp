#include "app/systems/PhysicsHost.h"

#include "core/Log.h"

#include <cmath>

namespace BigHero
{
void PhysicsHost::Init()
{
    engine.Init();
    engine.SetGravity(glm::vec3(0.0f, gravity, 0.0f));
}

void PhysicsHost::RebuildBodies()
{
    engine.RemoveAllBodies();
    // 清空物理映射（ECS PhysicsRef 组件归位 UINT32_MAX，替代原 physicsBodyIds_ 并行数组）
    for (Core::Entity e : ecsScene_.Order())
        ecsScene_.SetBodyId(e, UINT32_MAX);

    // 地面：静态大盒体（顶面 y=0，与渲染地面对齐）
    Physics::BodyConfig groundCfg;
    groundCfg.type = Physics::BodyType::Static;
    groundCfg.shape = Physics::ShapeType::Box;
    groundCfg.halfExtents = glm::vec3(500.0f, 0.5f, 500.0f); // 与渲染地面 1000×1000 对齐
    groundCfg.friction = 0.8f;
    groundCfg.restitution = 0.0f;
    engine.CreateBody(groundCfg, glm::vec3(0.0f, -0.5f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    // 场景物体：遍历 ECS 实体，按 PhysicsBody/Transform 组件创建刚体
    Core::Registry& reg = ecsScene_.Registry();
    const std::vector<Core::Entity>& order = ecsScene_.Order();
    for (size_t i = 0; i < order.size(); ++i)
    {
        const Core::Entity e = order[i];
        const Scene::ecs::PhysicsBody& pb = reg.Get<Scene::ecs::PhysicsBody>(e);
        if (pb.type == Physics::BodyType::None)
            continue;
        const Scene::ecs::Transform& t = reg.Get<Scene::ecs::Transform>(e);

        Physics::BodyConfig cfg;
        cfg.type = pb.type;
        cfg.shape = pb.shape;
        cfg.mass = pb.mass;
        cfg.friction = pb.friction;
        cfg.restitution = pb.restitution;
        cfg.halfExtents = glm::vec3(t.scale * 0.5f);
        cfg.radius = t.scale * 0.5f;
        cfg.capsuleHeight = t.scale * 0.5f;
        cfg.userTag = static_cast<uint32_t>(i); // 射线命中时返回包下标（选中索引语义不变）

        // 物体中心 position，旋转用欧拉角（自转 spinAngle 不参与物理，由渲染叠加）
        const glm::quat rot = glm::quat(glm::radians(t.rotation));
        ecsScene_.SetBodyId(e, engine.CreateBody(cfg, t.position, rot));
    }

    // 角色控制器：胶囊体动态刚体（半径0.4，身高1.0，质量80kg）
    characterBodyId = UINT32_MAX;
    if (characterEnabled)
    {
        Physics::BodyConfig charCfg;
        charCfg.type = Physics::BodyType::Dynamic;
        charCfg.shape = Physics::ShapeType::Capsule;
        charCfg.radius = 0.4f;
        charCfg.capsuleHeight = 1.0f;
        charCfg.mass = 80.0f;
        charCfg.friction = 0.0f; // 角色摩擦由速度控制，物理摩擦设为0防止粘墙
        charCfg.restitution = 0.0f;
        characterBodyId = engine.CreateBody(charCfg, characterSpawn, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }

    // 关节：在所有刚体创建后重建
    engine.DestroyAllJoints();
    jointIds.assign(joints.size(), UINT32_MAX);
    for (size_t i = 0; i < joints.size(); ++i)
    {
        const Physics::SceneJoint& sj = joints[i];
        if (sj.objectA >= scene_.size() || sj.objectB >= scene_.size())
            continue;
        const uint32_t bodyA = ecsScene_.BodyId(order[static_cast<size_t>(sj.objectA)]);
        const uint32_t bodyB = ecsScene_.BodyId(order[static_cast<size_t>(sj.objectB)]);
        if (bodyA == UINT32_MAX || bodyB == UINT32_MAX)
            continue;

        Physics::JointConfig jcfg;
        jcfg.type = sj.type;
        jcfg.body1Id = bodyA;
        jcfg.body2Id = bodyB;
        // 锚点取两物体中心的中点
        jcfg.anchor = (scene_[sj.objectA].position + scene_[sj.objectB].position) * 0.5f;
        jcfg.axis = sj.axis;
        jcfg.collisionEnabled = false;
        jointIds[i] = engine.CreateJoint(jcfg);
    }

    LOG_INFO("物理刚体重建: " << engine.BodyCount() << " 个（含地面" << (characterEnabled ? "+角色" : "")
                              << "），关节: " << engine.JointCount() << " 个");
}

void PhysicsHost::SyncBodies()
{
    // 运动学/静态体：ECS 变换组件同步到物理
    Core::Registry& reg = ecsScene_.Registry();
    for (Core::Entity e : ecsScene_.Order())
    {
        const uint32_t bodyId = ecsScene_.BodyId(e);
        if (bodyId == UINT32_MAX)
            continue;
        const Scene::ecs::PhysicsBody& pb = reg.Get<Scene::ecs::PhysicsBody>(e);
        if (pb.type == Physics::BodyType::Kinematic || pb.type == Physics::BodyType::Static)
        {
            const Scene::ecs::Transform& t = reg.Get<Scene::ecs::Transform>(e);
            const glm::quat rot = glm::quat(glm::radians(t.rotation));
            engine.SetBodyTransform(bodyId, t.position, rot);
        }
    }
}

void PhysicsHost::UpdateCharacter()
{
    if (!characterEnabled || characterBodyId == UINT32_MAX)
        return;

    // 每帧清零角速度，防止角色倒下
    engine.SetBodyAngularVelocity(characterBodyId, glm::vec3(0.0f));

    // 基于相机 yaw 计算移动方向（与 Camera::Pan 一致）
    const float yaw = camera_.Yaw();
    const glm::vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw));
    const glm::vec3 right(std::cos(yaw), 0.0f, -std::sin(yaw));

    glm::vec3 moveDir(0.0f);
    if (window_.IsKeyDown(Window::kKeyW))
        moveDir += forward;
    if (window_.IsKeyDown(Window::kKeyS))
        moveDir -= forward;
    if (window_.IsKeyDown(Window::kKeyD))
        moveDir += right;
    if (window_.IsKeyDown(Window::kKeyA))
        moveDir -= right;

    if (glm::length(moveDir) > 1e-6f)
        moveDir = glm::normalize(moveDir);

    // 保留当前 Y 速度（重力/跳跃），覆盖水平速度
    const glm::vec3 curVel = engine.GetBodyLinearVelocity(characterBodyId);
    glm::vec3 newVel = moveDir * characterSpeed;
    newVel.y = curVel.y;

    // 跳跃：空格 + 在地面
    if (window_.IsKeyDown(Window::kKeySpace) && characterGrounded)
        newVel.y = characterJumpForce;

    engine.SetBodyLinearVelocity(characterBodyId, newVel);
}

void PhysicsHost::Update(float dt)
{
    if (!enabled)
        return;

    // 角色控制器开关边沿检测：变更时重建刚体
    if (characterEnabled != prevCharacterEnabled)
    {
        prevCharacterEnabled = characterEnabled;
        RebuildBodies();
    }

    SyncBodies();
    UpdateCharacter(); // 步进前设置角色速度/跳跃
    engine.Step(dt);

    // 动态体：物理变换同步回 ECS Transform 组件（随 RepackScene 投影到渲染包）
    Core::Registry& reg = ecsScene_.Registry();
    for (Core::Entity e : ecsScene_.Order())
    {
        const uint32_t bodyId = ecsScene_.BodyId(e);
        if (bodyId == UINT32_MAX)
            continue;
        auto& pb = reg.Get<Scene::ecs::PhysicsBody>(e);
        if (pb.type != Physics::BodyType::Dynamic)
            continue;

        glm::vec3 pos;
        glm::quat rot;
        engine.GetBodyTransform(bodyId, pos, rot);
        Scene::ecs::Transform& t = reg.Get<Scene::ecs::Transform>(e);
        t.position = pos;
        t.rotation = glm::degrees(glm::eulerAngles(rot));
    }

    // 角色步进后：读取位置 + 地面检测 + 相机跟随
    if (characterEnabled && characterBodyId != UINT32_MAX)
    {
        glm::vec3 charPos;
        glm::quat charRot;
        engine.GetBodyTransform(characterBodyId, charPos, charRot);
        const glm::vec3 vel = engine.GetBodyLinearVelocity(characterBodyId);
        characterGrounded = std::abs(vel.y) < 0.5f;

        // 第三人称相机跟随：注视点 = 角色胸口高度
        camera_.SetTarget(charPos + glm::vec3(0.0f, 1.0f, 0.0f));

        // 角色掉出世界则重生
        if (charPos.y < -20.0f)
            engine.SetBodyTransform(characterBodyId, characterSpawn, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }
}
} // namespace BigHero
