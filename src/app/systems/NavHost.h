#pragma once
// 阶段 3d：导航子系统（自 Application 拆出）。
// 职责：A* 导航网格（演示网格构建 + 寻路）与 AI 导航代理（沿路径巡逻）的
//       初始化与每帧推进；调试可视化数据（网格/路径/代理调试线）经公有字段
//       供 Application::RecordUi 消费。
// 纯 CPU 逻辑（无 GPU 资源）；字段公有（编辑器开关经指针直写）。

#include "game/NavAgent.h"
#include "game/NavGrid.h"

#include <glm/glm.hpp>

namespace BigHero
{
class NavHost
{
  public:
    // ---- 导航网格（A*） ----
    Game::NavGrid grid;
    Game::PathResult path;          // 最近一次寻路结果（调试线消费）
    bool enabled = false;           // 是否在编辑器绘制导航调试线
    bool prevEnabled = false;       // 边沿检测，启用时重算路径
    int startX = 1, startY = 1;     // 寻路起点格
    int goalX = 14, goalY = 14;     // 寻路终点格
    float cellSize = 1.0f;          // 格宽（世界单位）
    glm::vec2 origin{-8.0f, -8.0f}; // 网格左下角世界坐标

    // ---- AI 导航代理（升级 18） ----
    Game::NavAgent agent;     // 沿 A* 路径移动、环形巡逻的 AI 代理
    bool agentEnabled = true; // AI 代理总开关（默认开启，可视化可在编辑器关闭）

    // 初始化：16x16 演示网格（八邻接 + Octile + 障碍簇）+ 代理绑定/巡逻点
    void Init();

    // 重算 A* 路径（起点/终点格变化或启用边沿时）
    void UpdatePath() { path = grid.FindPath(startX, startY, goalX, goalY); }

    // 每帧推进 AI 代理（沿路径插值移动；抵达巡逻点后自动规划到下一站）
    void UpdateAgent(float dt)
    {
        if (!agentEnabled)
            return;
        agent.Step(dt, cellSize, origin);
        if (agent.Arrived())
            agent.PlanToNext();
    }
};
} // namespace BigHero
