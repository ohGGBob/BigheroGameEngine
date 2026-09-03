#include "PhysicsEngine.h"
#include "core/Log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <reactphysics3d/constraint/BallAndSocketJoint.h>
#include <reactphysics3d/constraint/FixedJoint.h>
#include <reactphysics3d/constraint/HingeJoint.h>
#include <reactphysics3d/constraint/SliderJoint.h>
#include <reactphysics3d/reactphysics3d.h>
#include <utility>

namespace BigHero::Physics
{
namespace
{
// glm <-> rp3d 转换辅助
inline rp3d::Vector3 ToRp3d(const glm::vec3& v)
{
    return rp3d::Vector3(v.x, v.y, v.z);
}
inline rp3d::Quaternion ToRp3d(const glm::quat& q)
{
    return rp3d::Quaternion(q.x, q.y, q.z, q.w);
}
inline glm::vec3 ToGlm(const rp3d::Vector3& v)
{
    return glm::vec3(v.x, v.y, v.z);
}
inline glm::quat ToGlm(const rp3d::Quaternion& q)
{
    return glm::quat(q.w, q.x, q.y, q.z);
}

// 根据形状计算包围盒半尺寸（用于调试线框）
inline glm::vec3 ShapeHalfExtents(const BodyConfig& cfg)
{
    switch (cfg.shape)
    {
    case ShapeType::Sphere:
        return glm::vec3(cfg.radius);
    case ShapeType::Capsule:
        return glm::vec3(cfg.radius, cfg.radius + cfg.capsuleHeight * 0.5f, cfg.radius);
    case ShapeType::Box:
    default:
        return cfg.halfExtents;
    }
}

// 根据质量和形状体积计算密度（rp3d 用密度而非直接质量）
inline float ComputeDensity(const BodyConfig& cfg)
{
    float volume = 1.0f;
    switch (cfg.shape)
    {
    case ShapeType::Sphere:
        volume = (4.0f / 3.0f) * 3.14159265f * cfg.radius * cfg.radius * cfg.radius;
        break;
    case ShapeType::Capsule:
        volume = 3.14159265f * cfg.radius * cfg.radius * cfg.capsuleHeight +
                 (4.0f / 3.0f) * 3.14159265f * cfg.radius * cfg.radius * cfg.radius;
        break;
    case ShapeType::Box:
    default:
        volume = 8.0f * cfg.halfExtents.x * cfg.halfExtents.y * cfg.halfExtents.z;
        break;
    }
    return cfg.mass / std::max(volume, 1e-6f);
}
} // namespace

PhysicsEngine::PhysicsEngine() = default;

PhysicsEngine::~PhysicsEngine()
{
    Destroy();
}

void PhysicsEngine::Init()
{
    if (common_)
        return;
    common_ = std::make_unique<rp3d::PhysicsCommon>();
    rp3d::PhysicsWorld::WorldSettings settings;
    settings.gravity = rp3d::Vector3(0, -9.81f, 0);
    world_ = common_->createPhysicsWorld(settings);
    LOG_INFO("PhysicsEngine initialized (ReactPhysics3D)");
}

void PhysicsEngine::Destroy()
{
    if (!common_)
        return;
    RemoveAllBodies();
    if (world_)
    {
        common_->destroyPhysicsWorld(world_);
        world_ = nullptr;
    }
    common_.reset();
    LOG_INFO("PhysicsEngine destroyed");
}

void PhysicsEngine::Step(float deltaTime)
{
    if (!world_)
        return;
    // 固定步长 1/60，大 deltaTime 时分子步避免穿透
    const float fixedStep = 1.0f / 60.0f;
    float remaining = std::min(deltaTime, 0.1f); // 上限 100ms 防止螺旋
    while (remaining > fixedStep * 0.5f)
    {
        world_->update(fixedStep);
        remaining -= fixedStep;
    }
    if (remaining > 0.0f)
        world_->update(remaining);
}

uint32_t PhysicsEngine::CreateBody(const BodyConfig& config, const glm::vec3& position, const glm::quat& rotation)
{
    if (!world_ || config.type == BodyType::None)
        return UINT32_MAX;

    const rp3d::Transform transform(ToRp3d(position), ToRp3d(rotation));
    rp3d::RigidBody* body = world_->createRigidBody(transform);

    switch (config.type)
    {
    case BodyType::Static:
        body->setType(rp3d::BodyType::STATIC);
        break;
    case BodyType::Kinematic:
        body->setType(rp3d::BodyType::KINEMATIC);
        break;
    case BodyType::Dynamic:
    default:
        body->setType(rp3d::BodyType::DYNAMIC);
        break;
    }

    // 创建碰撞形状
    rp3d::CollisionShape* shape = nullptr;
    switch (config.shape)
    {
    case ShapeType::Sphere:
        shape = common_->createSphereShape(config.radius);
        break;
    case ShapeType::Capsule:
        shape = common_->createCapsuleShape(config.radius, config.capsuleHeight);
        break;
    case ShapeType::Box:
    default:
        shape = common_->createBoxShape(ToRp3d(config.halfExtents));
        break;
    }

    rp3d::Collider* collider = body->addCollider(shape, rp3d::Transform::identity());
    collider->getMaterial().setFrictionCoefficient(config.friction);
    collider->getMaterial().setBounciness(config.restitution);
    if (config.type == BodyType::Dynamic)
        collider->getMaterial().setMassDensity(ComputeDensity(config));

    const uint32_t id = static_cast<uint32_t>(bodies_.size());
    bodies_.push_back(body);
    configs_.push_back(config);
    active_.push_back(true);
    userTags_.push_back(config.userTag);
    return id;
}

void PhysicsEngine::RemoveBody(uint32_t id)
{
    if (id >= bodies_.size() || !active_[id])
        return;
    if (world_ && bodies_[id])
        world_->destroyRigidBody(bodies_[id]);
    bodies_[id] = nullptr;
    active_[id] = false;
}

void PhysicsEngine::RemoveAllBodies()
{
    if (!world_)
    {
        bodies_.clear();
        configs_.clear();
        active_.clear();
        userTags_.clear();
        return;
    }
    for (size_t i = 0; i < bodies_.size(); ++i)
    {
        if (active_[i] && bodies_[i])
            world_->destroyRigidBody(bodies_[i]);
    }
    bodies_.clear();
    configs_.clear();
    active_.clear();
    userTags_.clear();
}

void PhysicsEngine::SetBodyTransform(uint32_t id, const glm::vec3& position, const glm::quat& rotation)
{
    if (id >= bodies_.size() || !active_[id] || !bodies_[id])
        return;
    bodies_[id]->setTransform(rp3d::Transform(ToRp3d(position), ToRp3d(rotation)));
}

void PhysicsEngine::GetBodyTransform(uint32_t id, glm::vec3& outPosition, glm::quat& outRotation) const
{
    if (id >= bodies_.size() || !active_[id] || !bodies_[id])
        return;
    const rp3d::Transform t = bodies_[id]->getTransform();
    outPosition = ToGlm(t.getPosition());
    outRotation = ToGlm(t.getOrientation());
}

void PhysicsEngine::SetGravity(const glm::vec3& gravity)
{
    if (world_)
        world_->setGravity(ToRp3d(gravity));
}

std::vector<DebugLine> PhysicsEngine::GetDebugLines() const
{
    std::vector<DebugLine> lines;
    if (!world_)
        return lines;

    // 盒体 8 角点（局部空间）
    static const std::array<glm::vec3, 8> kBoxCorners = {
        glm::vec3(-1, -1, -1), glm::vec3(1, -1, -1), glm::vec3(1, 1, -1), glm::vec3(-1, 1, -1),
        glm::vec3(-1, -1, 1),  glm::vec3(1, -1, 1),  glm::vec3(1, 1, 1),  glm::vec3(-1, 1, 1)};
    // 12 条边（角点索引对）
    static const std::array<std::pair<int, int>, 12> kBoxEdges = {
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

    for (size_t i = 0; i < bodies_.size(); ++i)
    {
        if (!active_[i] || !bodies_[i])
            continue;

        const rp3d::Transform t = bodies_[i]->getTransform();
        const glm::vec3 pos = ToGlm(t.getPosition());
        const glm::quat rot = ToGlm(t.getOrientation());
        const glm::vec3 half = ShapeHalfExtents(configs_[i]);

        // 颜色：动态=绿，静态=红，运动学=蓝
        glm::vec3 color(0.5f, 0.5f, 0.5f);
        if (configs_[i].type == BodyType::Dynamic)
            color = glm::vec3(0.2f, 1.0f, 0.3f);
        else if (configs_[i].type == BodyType::Static)
            color = glm::vec3(1.0f, 0.3f, 0.2f);
        else if (configs_[i].type == BodyType::Kinematic)
            color = glm::vec3(0.2f, 0.5f, 1.0f);

        // 变换 8 个角点到世界空间
        std::array<glm::vec3, 8> worldCorners;
        for (int c = 0; c < 8; ++c)
        {
            const glm::vec3 local = kBoxCorners[c] * half;
            worldCorners[c] = pos + rot * local;
        }
        for (const auto& [a, b] : kBoxEdges)
            lines.push_back({worldCorners[a], worldCorners[b], color});
    }
    return lines;
}

void PhysicsEngine::SetBodyLinearVelocity(uint32_t id, const glm::vec3& velocity)
{
    if (id >= bodies_.size() || !active_[id] || !bodies_[id])
        return;
    bodies_[id]->setLinearVelocity(ToRp3d(velocity));
}

glm::vec3 PhysicsEngine::GetBodyLinearVelocity(uint32_t id) const
{
    if (id >= bodies_.size() || !active_[id] || !bodies_[id])
        return glm::vec3(0.0f);
    return ToGlm(bodies_[id]->getLinearVelocity());
}

void PhysicsEngine::SetBodyAngularVelocity(uint32_t id, const glm::vec3& velocity)
{
    if (id >= bodies_.size() || !active_[id] || !bodies_[id])
        return;
    bodies_[id]->setAngularVelocity(ToRp3d(velocity));
}

namespace
{
// 射线回调：收集最近命中
class ClosestHitCallback : public rp3d::RaycastCallback
{
  public:
    rp3d::Body* hitBody = nullptr;
    rp3d::Vector3 hitPoint{0, 0, 0};
    rp3d::Vector3 hitNormal{0, 0, 0};
    float hitFraction = 1.0f;
    bool hasHit = false;

    virtual rp3d::decimal notifyRaycastHit(const rp3d::RaycastInfo& info) override
    {
        if (info.body && (!hasHit || info.hitFraction < hitFraction))
        {
            hasHit = true;
            hitBody = info.body;
            hitPoint = info.worldPoint;
            hitNormal = info.worldNormal;
            hitFraction = static_cast<float>(info.hitFraction);
        }
        return 1.0; // 继续检测所有碰撞体，最后取最近
    }
};
} // namespace

RaycastHit PhysicsEngine::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const
{
    RaycastHit result;
    if (!world_)
        return result;

    const glm::vec3 dir = glm::length(direction) > 1e-8f ? glm::normalize(direction) : glm::vec3(0, 0, 1);
    const rp3d::Ray ray(ToRp3d(origin), ToRp3d(origin + dir * maxDistance));

    ClosestHitCallback callback;
    world_->raycast(ray, &callback, 0xFFFF);

    if (!callback.hasHit)
        return result;

    // 映射 body* 到 userTag
    for (size_t i = 0; i < bodies_.size(); ++i)
    {
        if (active_[i] && bodies_[i] == static_cast<rp3d::RigidBody*>(callback.hitBody))
        {
            result.hit = true;
            result.userTag = userTags_[i];
            result.point = ToGlm(callback.hitPoint);
            result.normal = ToGlm(callback.hitNormal);
            result.distance = callback.hitFraction * maxDistance;
            return result;
        }
    }
    return result;
}

uint32_t PhysicsEngine::CreateJoint(const JointConfig& config)
{
    if (!world_ || config.body1Id >= bodies_.size() || config.body2Id >= bodies_.size())
        return UINT32_MAX;
    if (!active_[config.body1Id] || !active_[config.body2Id])
        return UINT32_MAX;

    rp3d::RigidBody* body1 = bodies_[config.body1Id];
    rp3d::RigidBody* body2 = bodies_[config.body2Id];
    const rp3d::Vector3 anchor = ToRp3d(config.anchor);
    const rp3d::Vector3 axis =
        ToRp3d(glm::length(config.axis) > 1e-6f ? glm::normalize(config.axis) : glm::vec3(0, 1, 0));

    rp3d::Joint* joint = nullptr;
    switch (config.type)
    {
    case JointType::Fixed:
    {
        rp3d::FixedJointInfo info(body1, body2, anchor);
        info.isCollisionEnabled = config.collisionEnabled;
        joint = world_->createJoint(info);
        break;
    }
    case JointType::Hinge:
    {
        rp3d::HingeJointInfo info(body1, body2, anchor, axis);
        info.isCollisionEnabled = config.collisionEnabled;
        joint = world_->createJoint(info);
        break;
    }
    case JointType::BallAndSocket:
    {
        rp3d::BallAndSocketJointInfo info(body1, body2, anchor);
        info.isCollisionEnabled = config.collisionEnabled;
        joint = world_->createJoint(info);
        break;
    }
    case JointType::Slider:
    {
        rp3d::SliderJointInfo info(body1, body2, anchor, axis);
        info.isCollisionEnabled = config.collisionEnabled;
        joint = world_->createJoint(info);
        break;
    }
    }

    if (!joint)
        return UINT32_MAX;

    joints_.push_back(joint);
    jointConfigs_.push_back(config);
    return static_cast<uint32_t>(joints_.size() - 1);
}

void PhysicsEngine::DestroyJoint(uint32_t jointId)
{
    if (!world_ || jointId >= joints_.size() || !joints_[jointId])
        return;
    world_->destroyJoint(joints_[jointId]);
    joints_[jointId] = nullptr;
}

void PhysicsEngine::DestroyAllJoints()
{
    if (!world_)
    {
        joints_.clear();
        jointConfigs_.clear();
        return;
    }
    for (rp3d::Joint* j : joints_)
        if (j)
            world_->destroyJoint(j);
    joints_.clear();
    jointConfigs_.clear();
}

JointInfo PhysicsEngine::GetJointInfo(uint32_t jointId) const
{
    JointInfo info{};
    if (jointId >= jointConfigs_.size())
        return info;
    const JointConfig& cfg = jointConfigs_[jointId];
    info.type = cfg.type;
    info.body1Id = cfg.body1Id;
    info.body2Id = cfg.body2Id;
    info.anchor = cfg.anchor;
    info.axis = cfg.axis;
    return info;
}
} // namespace BigHero::Physics
