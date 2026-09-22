// NavMesh 后端接线单元测试：纯逻辑、零 GPU，可离线运行。
// 验证 NavHost 可在 NavGrid（默认）与 NavMesh（可选）双后端间切换并返回路径。
#include "framework/test_common.h"
#include "app/systems/NavHost.h"

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
    host.startX = 1; host.startY = 1;
    host.goalX = 14; host.goalY = 14;
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
