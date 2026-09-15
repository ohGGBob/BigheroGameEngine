#pragma once
// ECS 场景实体化：场景物体由 Core::Registry 实体 + 组件驱动（Roadmap「ECS 场景实体化」）。
// 纯 CPU、仅标准库 + glm，可离线单元测试（不触碰 Vulkan/GPU）。
//
// 设计：
//   - 组件：Transform（位置/欧拉旋转/均匀缩放）、Renderable（网格+PBR 材质）、
//           Spin（自转参数 + 运行时角）、PhysicsBody（刚体配置）、PhysicsRef（物理体 ID 映射），
//           字段与 SceneObject 一一对应，包投影/写回零信息损失。
//   - EcsScene 世界：实体生命周期（创建/按稳定序销毁/全量重建）+ 组件化更新系统
//     （自转系统 UpdateSpins）+ 与 SceneObject 包的双向同步。
//   - 包（packet）= std::vector<SceneObject>：既有渲染/编辑器/拾取/序列化/撤销路径的兼容层。
//     ECS 是权威存储；每帧 BuildPacket 投影输出，编辑器/UI 对包的修改经 SyncFromPacket 写回。
//   - 稳定序：SparseSet 迭代序会被 swap-pop 打乱，order_ 维护"包下标 -> 实体"的稳定映射，
//     保证选中索引/物理 userTag/关节 objectA/B 等既有索引语义不变。

#include "core/ecs.h"
#include "scene/Scene.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace BigHero::Scene::ecs
{
// ---- 场景物体组件（与 SceneObject 字段一一对应） ----

// 空间变换：位置 + 欧拉旋转（度，XYZ 顺序，编辑器语义）+ 均匀缩放。
// 自转不在此处叠加——Spin 组件独立保存运行时角，渲染时与欧拉角合成模型矩阵。
struct Transform
{
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // 欧拉 XYZ（度）
    float scale = 1.0f;
};

// 渲染表现：网格引用 + PBR 材质（经实例缓冲/推送常量下传）。
struct Renderable
{
    uint32_t meshId = 0; // 0=共享立方体 1=torus 2=glTF
    glm::vec3 tint{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

// 自转：speed/phase 为编辑器可调参数，angle 为运行时状态（度，随帧积分）。
struct Spin
{
    float speed = 0.0f;
    float phase = 0.0f;
    float angle = 0.0f;
};

// 物理刚体配置：RebuildPhysicsBodies 据此创建/销毁物理体（None=不参与物理）。
struct PhysicsBody
{
    Physics::BodyType type = Physics::BodyType::None;
    Physics::ShapeType shape = Physics::ShapeType::Box;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
};

// 物理运行时映射：物理引擎刚体 ID（UINT32_MAX=未创建）。
struct PhysicsRef
{
    uint32_t bodyId = UINT32_MAX;
};
} // namespace BigHero::Scene::ecs

namespace BigHero::Scene
{
// ECS 实体模型矩阵：与 SceneObject 路径共用 Scene 核心函数，保证渲染直读 ECS 时矩阵逐位一致。
[[nodiscard]] inline glm::mat4 ComputeEntityModelMatrix(const ecs::Transform& t, float spinAngleDeg)
{
    return ComputeObjectModelMatrix(t.position, t.rotation, t.scale, spinAngleDeg);
}

// ECS 场景世界：实体生命周期 + 组件存储 + 组件化更新系统 + SceneObject 包互转。
class EcsScene
{
  public:
    EcsScene() = default;

    // 从 SceneObject 创建场景实体（Transform/Renderable/Spin/PhysicsBody/PhysicsRef 齐套），
    // 追加到稳定序末尾。自转角初始化为 phase（与原 spinAngles_ 初始化语义一致）。
    [[nodiscard]] Core::Entity CreateObject(const SceneObject& obj)
    {
        Core::Entity e = registry_.Create();

        auto& t = registry_.Add<ecs::Transform>(e);
        t.position = obj.position;
        t.rotation = obj.rotation;
        t.scale = obj.scale;

        auto& r = registry_.Add<ecs::Renderable>(e);
        r.meshId = obj.meshId;
        r.tint = obj.tint;
        r.metallic = obj.metallic;
        r.roughness = obj.roughness;

        auto& s = registry_.Add<ecs::Spin>(e);
        s.speed = obj.spinSpeed;
        s.phase = obj.phase;
        s.angle = obj.phase;

        auto& pb = registry_.Add<ecs::PhysicsBody>(e);
        pb.type = obj.physicsType;
        pb.shape = obj.physicsShape;
        pb.mass = obj.physicsMass;
        pb.friction = obj.physicsFriction;
        pb.restitution = obj.physicsRestitution;

        registry_.Add<ecs::PhysicsRef>(e); // bodyId = UINT32_MAX

        order_.push_back(e);
        return e;
    }

    // 按稳定序销毁第 orderIndex 个物体，其余物体相对顺序保持不变（包下标随之收缩）。
    void DestroyAt(size_t orderIndex)
    {
        if (orderIndex >= order_.size())
            return;
        registry_.Destroy(order_[orderIndex]);
        order_.erase(order_.begin() + static_cast<std::ptrdiff_t>(orderIndex));
    }

    // 全量重建：销毁全部实体后按 objs 依次创建（undo 恢复 / 读档 / 默认场景）。
    // spins 非空时以 spins[i] 初始化第 i 个物体的自转角，否则用 phase。
    void LoadPacket(const std::vector<SceneObject>& objs, const std::vector<float>* spins = nullptr)
    {
        registry_.DestroyAll();
        order_.clear();
        order_.reserve(objs.size());
        for (size_t i = 0; i < objs.size(); ++i)
        {
            const Core::Entity e = CreateObject(objs[i]);
            if (spins != nullptr && i < spins->size())
                registry_.Get<ecs::Spin>(e).angle = (*spins)[i];
        }
    }

    // ---- 组件化更新系统 ----

    // 自转系统：Spin.angle += speed * dt，>=360 回绕（与原 UpdateTime 循环语义一致）。
    void UpdateSpins(float dt)
    {
        Core::MakeView<ecs::Spin>(registry_).Each(
            [dt](ecs::Spin& s)
            {
                s.angle += s.speed * dt;
                if (s.angle >= 360.0f)
                    s.angle -= 360.0f;
            });
    }

    // ---- ECS -> SceneObject 包投影（渲染/编辑器/拾取/序列化兼容层） ----

    // 逐稳定序实体组装 SceneObject 包；spinAngles 非空时输出各实体当前自转角（与包同序）。
    [[nodiscard]] std::vector<SceneObject> BuildPacket(std::vector<float>* spinAngles = nullptr) const
    {
        std::vector<SceneObject> objs;
        objs.reserve(order_.size());
        if (spinAngles != nullptr)
            spinAngles->clear();
        for (const Core::Entity e : order_)
        {
            const ecs::Transform& t = registry_.Get<ecs::Transform>(e);
            const ecs::Renderable& r = registry_.Get<ecs::Renderable>(e);
            const ecs::Spin& s = registry_.Get<ecs::Spin>(e);

            SceneObject o;
            o.position = t.position;
            o.rotation = t.rotation;
            o.scale = t.scale;
            o.meshId = r.meshId;
            o.tint = r.tint;
            o.metallic = r.metallic;
            o.roughness = r.roughness;
            o.spinSpeed = s.speed;
            o.phase = s.phase;
            if (const ecs::PhysicsBody* pb = registry_.TryGet<ecs::PhysicsBody>(e))
            {
                o.physicsType = pb->type;
                o.physicsShape = pb->shape;
                o.physicsMass = pb->mass;
                o.physicsFriction = pb->friction;
                o.physicsRestitution = pb->restitution;
            }
            objs.push_back(o);
            if (spinAngles != nullptr)
                spinAngles->push_back(s.angle);
        }
        return objs;
    }

    // 包 -> ECS 写回（编辑器属性 / Gizmo 拖拽等对包的修改持久化）。
    // 要求 objs.size() == ObjectCount()：尺寸不一致说明发生了结构性变更，
    // 必须走 LoadPacket 全量重建，此处直接忽略以防错位写回。
    // 自转角 angle 为 ECS 运行时状态，不参与写回（包中的 spinAngles 仅是投影输出）。
    void SyncFromPacket(const std::vector<SceneObject>& objs)
    {
        if (objs.size() != order_.size())
            return;
        for (size_t i = 0; i < objs.size(); ++i)
        {
            const SceneObject& o = objs[i];
            const Core::Entity e = order_[i];

            auto& t = registry_.Get<ecs::Transform>(e);
            t.position = o.position;
            t.rotation = o.rotation;
            t.scale = o.scale;

            auto& r = registry_.Get<ecs::Renderable>(e);
            r.meshId = o.meshId;
            r.tint = o.tint;
            r.metallic = o.metallic;
            r.roughness = o.roughness;

            auto& s = registry_.Get<ecs::Spin>(e);
            s.speed = o.spinSpeed;
            s.phase = o.phase;

            if (auto* pb = registry_.TryGet<ecs::PhysicsBody>(e))
            {
                pb->type = o.physicsType;
                pb->shape = o.physicsShape;
                pb->mass = o.physicsMass;
                pb->friction = o.physicsFriction;
                pb->restitution = o.physicsRestitution;
            }
        }
    }

    // ---- 渲染端直读（ECS 渲染收敛：渲染不再经 SceneObject 包投影） ----

    // 稳定序遍历渲染三元组（Transform/Renderable/Spin，全部实体五件套齐套故直接 Get）。
    // 手写 order_ 循环而非 View<T...>::Each：后者按组件池 dense 序迭代，swap-pop 会打乱，
    // 无法保证与包一致的稳定实例顺序。
    template <typename Fn>
    void ForEachRenderable(Fn&& fn) const
    {
        for (const Core::Entity e : order_)
        {
            const ecs::Transform& t = registry_.Get<ecs::Transform>(e);
            const ecs::Renderable& r = registry_.Get<ecs::Renderable>(e);
            const ecs::Spin& s = registry_.Get<ecs::Spin>(e);
            fn(t, r, s);
        }
    }

    // ---- 访问 ----

    [[nodiscard]] size_t ObjectCount() const noexcept { return order_.size(); }
    [[nodiscard]] Core::Entity At(size_t orderIndex) const noexcept { return order_[orderIndex]; }
    [[nodiscard]] const std::vector<Core::Entity>& Order() const noexcept { return order_; }
    [[nodiscard]] const Core::Registry& Registry() const noexcept { return registry_; }
    [[nodiscard]] Core::Registry& Registry() noexcept { return registry_; }

    // 物理运行时映射：实体 -> 物理引擎刚体 ID（UINT32_MAX=无）。
    [[nodiscard]] uint32_t BodyId(Core::Entity e) const noexcept
    {
        const ecs::PhysicsRef* ref = registry_.TryGet<ecs::PhysicsRef>(e);
        return ref != nullptr ? ref->bodyId : UINT32_MAX;
    }
    void SetBodyId(Core::Entity e, uint32_t bodyId) noexcept
    {
        if (auto* ref = registry_.TryGet<ecs::PhysicsRef>(e))
            ref->bodyId = bodyId;
    }

  private:
    Core::Registry registry_;
    std::vector<Core::Entity> order_; // 稳定序：包下标 -> 实体（SparseSet swap-pop 会打乱 dense 序）
};
} // namespace BigHero::Scene
