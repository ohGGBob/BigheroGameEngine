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
} // namespace BigHero
