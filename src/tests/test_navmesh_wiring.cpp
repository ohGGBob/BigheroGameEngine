// NavMesh 后端接线单元测试：纯逻辑、零 GPU，可离线运行。
// 验证 NavHost 可在 NavGrid（默认）与 NavMesh（可选）双后端间切换并返回路径。
#include "app/systems/NavHost.h"
#include "framework/test_common.h"
#include "scene/EcsScene.h"
#include "scene/Scene.h"

using namespace BigHero;

TEST_CASE("NavMeshWiring.DefaultBackendIsNavGrid")
{
    NavHost host;
    host.Init();
    // 默认：useNavMesh=false，走 NavGrid
    CHECK(host.useNavMesh == false);
    CHECK(host.path.found); // NavGrid 16x16 演示网格有路径
}

TEST_CASE("NavMeshWiring.SwitchToNavMeshReturnsPath")
{
    NavHost host;
    host.Init();
    host.BuildNavMeshDemo();
    host.useNavMesh = true;
    // 起点 (1,1) → 终点 (14,14)，与 NavGrid 演示网格对齐
    host.startX = 1;
    host.startY = 1;
    host.goalX = 14;
    host.goalY = 14;
    host.UpdatePath();
    CHECK(host.navPath.found);
    CHECK_GE(host.navPath.points.size(), 2u);
    CHECK_GT(host.navPath.length, 0.0f);
}

TEST_CASE("NavMeshWiring.SwitchBackToNavGrid")
{
    NavHost host;
    host.Init();
    host.BuildNavMeshDemo();
    host.useNavMesh = true;
    host.UpdatePath();
    CHECK(host.navPath.found);

    // 切回 NavGrid
    host.useNavMesh = false;
    host.UpdatePath();
    CHECK(host.path.found);
}
// 从真实 ECS 场景几何（立方体）烘焙 NavMesh：地面 + 障碍箱应产出可行走多边形。
TEST_CASE("NavMeshWiring.BuildFromEcsSceneWithCubesProducesPolys")
{
    Scene::EcsScene ecs;
    std::vector<Scene::SceneObject> objs;
    // 宽大地面立方体（scale=10，中心 y=5 -> 顶面 y=10，10m x 10m 可行走面）。
    objs.push_back(Scene::SceneObject{glm::vec3(0.0f, 5.0f, 0.0f), 10.0f, glm::vec3(1.0f), 0.0f, 0.0f, 0u, 0.0f, 0.5f});
    // 地面上的小障碍箱（scale=1，中心 y=10.5 -> 底面贴地 y=10）。
    objs.push_back(Scene::SceneObject{glm::vec3(2.0f, 10.5f, 0.0f), 1.0f, glm::vec3(1.0f), 0.0f, 0.0f, 0u, 0.0f, 0.5f});
    ecs.LoadPacket(objs);

    NavHost host;
    host.Init();
    CHECK(host.BuildFromEcsScene(ecs));
    CHECK_GT(host.navMesh.PolyCount(), 0);
}

// 空 ECS 场景：构建仍算成功，但不产出多边形（PolyCount == 0）。
TEST_CASE("NavMeshWiring.BuildFromEcsSceneEmptyIsSuccessWithNoPolys")
{
    Scene::EcsScene ecs; // 默认构造，无任何实体
    NavHost host;
    host.Init();
    CHECK(host.BuildFromEcsScene(ecs));
    CHECK_EQ(host.navMesh.PolyCount(), 0);
}

// 含非立方体网格（torus/glTF）的实体应被跳过，仅立方体参与烘焙。
TEST_CASE("NavMeshWiring.BuildFromEcsSceneSkipsNonCubeMeshes")
{
    Scene::EcsScene ecs;
    std::vector<Scene::SceneObject> objs;
    // 地面立方体（可行走）
    objs.push_back(Scene::SceneObject{glm::vec3(0.0f, 5.0f, 0.0f), 10.0f, glm::vec3(1.0f), 0.0f, 0.0f, 0u, 0.0f, 0.5f});
    // torus(1) / glTF(2)：CPU 顶点不可访问，应被跳过而非崩溃
    objs.push_back(Scene::SceneObject{glm::vec3(0.0f, 0.5f, 0.0f), 1.0f, glm::vec3(1.0f), 0.0f, 0.0f, 1u, 0.0f, 0.5f});
    objs.push_back(Scene::SceneObject{glm::vec3(1.0f, 0.5f, 1.0f), 1.0f, glm::vec3(1.0f), 0.0f, 0.0f, 2u, 0.0f, 0.5f});
    ecs.LoadPacket(objs);

    NavHost host;
    host.Init();
    CHECK(host.BuildFromEcsScene(ecs));
    CHECK_GT(host.navMesh.PolyCount(), 0);
}
