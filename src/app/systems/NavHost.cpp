#include "app/systems/NavHost.h"

#include "core/Log.h"

namespace BigHero
{
void NavHost::Init()
{
    // ---- 导航网格：16x16 演示网格，八邻接 + Octile 启发式 ----
    grid.Resize(16, 16, /*allowDiagonal=*/true);
    grid.SetHeuristic(Game::NavHeuristic::Octile);
    // 放置若干障碍簇，营造需绕行的寻路场景
    const int blocks[][2] = {{4, 4}, {4, 5}, {5, 4}, {8, 8}, {8, 9}, {9, 8}, {12, 3}, {3, 12}};
    for (const auto& b : blocks)
        grid.SetBlocked(b[0], b[1], true);
    UpdatePath();
    LOG_INFO("导航网格初始化: " << grid.Width() << "x" << grid.Height() << " 八邻接，路径 "
                                << (path.found ? "已找到" : "未找到"));

    // ---- 升级 18：AI 导航代理（NavAgent） ----
    // 绑定导航网格，复用同一世界映射（格宽 + 左下角原点），设定巡逻点（四角空闲格，避开障碍簇）。
    agent.BindGrid(&grid);
    agent.SetWorldMapping(cellSize, origin);
    agent.SetSpeed(3.0f); // 3 格/秒
    agent.SetPatrolPoints({Game::Cell{1, 1}, Game::Cell{14, 1}, Game::Cell{14, 14}, Game::Cell{1, 14}});
    agent.Plan(Game::Cell{1, 1}, Game::Cell{1, 1}); // 起始于首个巡逻点
    LOG_INFO("AI 导航代理初始化: 巡逻点 4，速度 " << agent.Speed() << " 格/秒");
}

void NavHost::BuildNavMeshDemo()
{
    // 程序化演示场景：16x16 地面（y=0）+ 中心 2x2 障碍箱（y=0..1）。
    // 与 NavGrid 的 16x16 网格对齐，世界坐标原点 = origin，格宽 = cellSize。
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> idx;
    auto addTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        const uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back(a); verts.push_back(b); verts.push_back(c);
        idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
    };
    auto addQuad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
        addTri(a, b, c); addTri(a, c, d);
    };
    const float x0 = origin.x, z0 = origin.y;
    const float x1 = origin.x + 16.0f * cellSize, z1 = origin.y + 16.0f * cellSize;
    addQuad({x0, 0.0f, z0}, {x1, 0.0f, z0}, {x1, 0.0f, z1}, {x0, 0.0f, z1});
    // 中心障碍箱（4..6, 4..6），与 NavGrid 障碍簇对齐
    const float bx0 = x0 + 4.0f * cellSize, bx1 = x0 + 6.0f * cellSize;
    const float bz0 = z0 + 4.0f * cellSize, bz1 = z0 + 6.0f * cellSize;
    addQuad({bx0, 1.0f, bz0}, {bx1, 1.0f, bz0}, {bx1, 1.0f, bz1}, {bx0, 1.0f, bz1});

    Navigation::NavBuildSettings s;
    s.walkableRadius = 1;
    navMesh.Build(verts.data(), verts.size(), idx.data(), idx.size(), s);
    LOG_INFO("NavMesh 演示场景构建: verts=" << verts.size() << " polys=" << navMesh.PolyCount());
}
} // namespace BigHero
