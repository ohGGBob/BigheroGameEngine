// ECS 场景实体化单元测试：实体生命周期 / 组件投影与写回 / 自转系统 / 物理映射。
// 纯 CPU（EcsScene 不触碰 Vulkan），验证 ECS 权威存储与 SceneObject 包的往返一致性。
#include "framework/test_common.h"
#include "scene/EcsScene.h"
#include "scene/Scene.h"

using namespace BigHero;

namespace
{
Scene::SceneObject MakeCube(glm::vec3 pos, float spinSpeed = 0.0f)
{
    Scene::SceneObject o;
    o.position = pos;
    o.scale = 1.0f;
    o.tint = glm::vec3(1.0f);
    o.spinSpeed = spinSpeed;
    o.phase = 0.0f; // SceneObject 的 phase 无默认初始化器，显式清零保证确定性
    o.metallic = 0.0f;
    o.roughness = 0.5f;
    o.rotation = glm::vec3(0.0f);
    o.meshId = 0;
    return o;
}
} // namespace

TEST_CASE("EcsScene.CreateAndProject")
{
    // ---- 创建 -> 投影：组件字段与源包零信息损失，初始自转角 = phase ----
    Scene::EcsScene world;
    const std::vector<Scene::SceneObject> src = Scene::BuildDefaultScene();
    for (const Scene::SceneObject& o : src)
        (void)world.CreateObject(o);
    CHECK(world.ObjectCount() == src.size());

    std::vector<float> spins;
    const std::vector<Scene::SceneObject> packet = world.BuildPacket(&spins);
    CHECK(packet.size() == src.size());
    CHECK(spins.size() == src.size());
    for (size_t i = 0; i < src.size(); ++i)
    {
        CHECK(packet[i].position == src[i].position);
        CHECK(packet[i].rotation == src[i].rotation);
        CHECK(packet[i].scale == src[i].scale);
        CHECK(packet[i].meshId == src[i].meshId);
        CHECK(packet[i].tint == src[i].tint);
        CHECK(packet[i].metallic == src[i].metallic);
        CHECK(packet[i].roughness == src[i].roughness);
        CHECK(packet[i].spinSpeed == src[i].spinSpeed);
        CHECK(packet[i].phase == src[i].phase);
        CHECK(packet[i].physicsType == src[i].physicsType);
        CHECK(packet[i].physicsShape == src[i].physicsShape);
        CHECK(packet[i].physicsMass == src[i].physicsMass);
        CHECK(packet[i].physicsFriction == src[i].physicsFriction);
        CHECK(packet[i].physicsRestitution == src[i].physicsRestitution);
        CHECK(spins[i] == src[i].phase);
    }
}

TEST_CASE("EcsScene.UpdateSpins")
{
    // ---- 自转系统：angle += speed*dt，>=360 回绕 ----
    Scene::EcsScene world;
    Scene::SceneObject a = MakeCube(glm::vec3(0.0f), 90.0f);
    a.phase = 10.0f;
    (void)world.CreateObject(a);
    (void)world.CreateObject(MakeCube(glm::vec3(1.0f, 0.0f, 0.0f), 0.0f)); // 静止物体

    world.UpdateSpins(1.0f);
    std::vector<float> spins;
    (void)world.BuildPacket(&spins);
    CHECK_NEAR(spins[0], 100.0f, 1e-4f); // phase 10 + 90°/s * 1s
    CHECK_NEAR(spins[1], 0.0f, 1e-4f);

    // 100 + 90*3 = 370 -> 回绕 10
    world.UpdateSpins(3.0f);
    (void)world.BuildPacket(&spins);
    CHECK_NEAR(spins[0], 10.0f, 1e-4f);

    // speed 写回生效：改为 0 后不再积分
    auto packet = world.BuildPacket();
    packet[0].spinSpeed = 0.0f;
    world.SyncFromPacket(packet);
    world.UpdateSpins(5.0f);
    (void)world.BuildPacket(&spins);
    CHECK_NEAR(spins[0], 10.0f, 1e-4f);
}

TEST_CASE("EcsScene.DestroyKeepsOrder")
{
    // ---- 按稳定序销毁：其余物体相对顺序与包下标保持 ----
    Scene::EcsScene world;
    std::vector<Core::Entity> es;
    for (int i = 0; i < 4; ++i)
        es.push_back(world.CreateObject(MakeCube(glm::vec3(float(i), 0.0f, 0.0f))));
    REQUIRE(world.ObjectCount() == 4);

    world.DestroyAt(1);
    CHECK(world.ObjectCount() == 3);
    const std::vector<Scene::SceneObject> packet = world.BuildPacket();
    CHECK(packet[0].position.x == 0.0f);
    CHECK(packet[1].position.x == 2.0f);
    CHECK(packet[2].position.x == 3.0f);

    // 旧句柄失效（版本递增），新实体不可混淆
    CHECK(!world.Registry().Alive(es[1]));
    CHECK(world.Registry().Alive(es[0]));
    CHECK(world.Registry().Alive(es[2]));

    // 越界销毁无副作用
    world.DestroyAt(99);
    CHECK(world.ObjectCount() == 3);

    // 销毁后新建：复用 index 但版本递增，稳定序追加末尾
    const Core::Entity fresh = world.CreateObject(MakeCube(glm::vec3(9.0f, 0.0f, 0.0f)));
    CHECK(world.Registry().Alive(fresh));
    CHECK(world.ObjectCount() == 4);
    CHECK(world.At(3) == fresh);
    CHECK(fresh != es[1]);
}

TEST_CASE("EcsScene.SyncFromPacket")
{
    // ---- 包写回：编辑器修改持久化到组件，自转角不被覆盖 ----
    Scene::EcsScene world;
    (void)world.CreateObject(MakeCube(glm::vec3(0.0f), 30.0f));
    world.UpdateSpins(1.0f); // angle = 30

    auto packet = world.BuildPacket();
    packet[0].position = glm::vec3(5.0f, 6.0f, 7.0f);
    packet[0].tint = glm::vec3(0.1f, 0.2f, 0.3f);
    packet[0].metallic = 1.0f;
    packet[0].roughness = 0.25f;
    packet[0].rotation = glm::vec3(0.0f, 45.0f, 0.0f);
    packet[0].scale = 2.5f;
    packet[0].physicsType = Physics::BodyType::Dynamic;
    world.SyncFromPacket(packet);

    std::vector<float> spins;
    const std::vector<Scene::SceneObject> back = world.BuildPacket(&spins);
    CHECK(back[0].position == glm::vec3(5.0f, 6.0f, 7.0f));
    CHECK(back[0].tint == glm::vec3(0.1f, 0.2f, 0.3f));
    CHECK(back[0].metallic == 1.0f);
    CHECK(back[0].roughness == 0.25f);
    CHECK(back[0].rotation == glm::vec3(0.0f, 45.0f, 0.0f));
    CHECK(back[0].scale == 2.5f);
    CHECK(back[0].physicsType == Physics::BodyType::Dynamic);
    CHECK_NEAR(spins[0], 30.0f, 1e-4f); // 运行时自转角不被写回覆盖

    // 尺寸不一致（结构性变更）拒绝写回
    packet.push_back(MakeCube(glm::vec3(8.0f)));
    world.SyncFromPacket(packet);
    CHECK(world.ObjectCount() == 1);
}

TEST_CASE("EcsScene.LoadPacketRestore")
{
    // ---- 全量重建（undo 恢复/读档路径）：物体与自转角一并恢复 ----
    Scene::EcsScene world;
    for (int i = 0; i < 3; ++i)
        (void)world.CreateObject(MakeCube(glm::vec3(float(i), 0.0f, 0.0f), 45.0f));
    world.UpdateSpins(2.0f); // angle = 90

    std::vector<float> spins;
    const std::vector<Scene::SceneObject> snap = world.BuildPacket(&spins);

    // 破坏性修改后整体重建
    world.DestroyAt(0);
    world.LoadPacket(snap, &spins);
    CHECK(world.ObjectCount() == 3);

    std::vector<float> spins2;
    const std::vector<Scene::SceneObject> restored = world.BuildPacket(&spins2);
    for (size_t i = 0; i < snap.size(); ++i)
    {
        CHECK(restored[i].position == snap[i].position);
        CHECK(restored[i].spinSpeed == snap[i].spinSpeed);
        CHECK_NEAR(spins2[i], spins[i], 1e-4f);
    }
}

TEST_CASE("EcsScene.PhysicsRefMapping")
{
    // ---- 物理体 ID 映射（替代原 physicsBodyIds_ 并行数组） ----
    Scene::EcsScene world;
    const Core::Entity e = world.CreateObject(MakeCube(glm::vec3(0.0f)));
    CHECK(world.BodyId(e) == UINT32_MAX);
    world.SetBodyId(e, 7u);
    CHECK(world.BodyId(e) == 7u);

    // 销毁实体后查询安全返回"无物理体"
    world.DestroyAt(0);
    CHECK(world.BodyId(e) == UINT32_MAX);
}
