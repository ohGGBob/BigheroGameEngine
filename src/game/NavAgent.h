#pragma once
// AI 导航代理（NavAgent，纯 CPU 逻辑，无 GPU/窗口依赖，可离线单测）。
//
// 设计：
//   - NavAgent 不拥有导航网格，仅持有其指针引用；规划时委托 NavGrid::FindPath。
//   - 路径跟随：规划成功后，代理沿路径逐格前进，世界坐标用恒定世界速度线性插值
//     （speed_ 以"格/秒"给定，世界速度 = speed_ * cellSize），单帧可跨多格。
//   - 巡逻（Patrol）：一组环形巡逻点，抵达当前目标后自动规划到下一个巡逻点，
//     形成闭环（points[0] -> points[1] -> ... -> points[n-1] -> points[0] -> ...）。
//   - 世界坐标映射与 NavGrid::GetDebugLines 完全一致：cell 中心 = origin + (x+0.5, y+0.5)*cellSize，
//     映射到世界 x/z，y=0 平面。编辑器可借 GetDebugLines() 复用同一调试线绘制管线。
//
// 该模块是升级 18 的核心，复用升级 17 的 A* 导航网格，纯逻辑可单测、编辑器可可视化。

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "game/NavGrid.h"

namespace BigHero::Game
{
class NavAgent
{
  public:
    explicit NavAgent(const NavGrid* grid = nullptr) : grid_(grid) {}

    // 绑定 / 解绑网格（代理不拥有网格，调用方负责其生命周期）。
    void BindGrid(const NavGrid* grid) noexcept { grid_ = grid; }
    [[nodiscard]] const NavGrid* Grid() const noexcept { return grid_; }

    // 世界映射：格宽 cellSize，网格左下角世界坐标 origin。规划与插值均依赖此映射。
    void SetWorldMapping(float cellSize, const glm::vec2& origin) noexcept
    {
        cellSize_ = cellSize;
        origin_ = origin;
    }
    [[nodiscard]] float CellSize() const noexcept { return cellSize_; }
    [[nodiscard]] glm::vec2 Origin() const noexcept { return origin_; }

    // 移动速度（格/秒）。世界速度 = speed_ * cellSize_。
    void SetSpeed(float cellsPerSec) noexcept { speed_ = std::max(0.0f, cellsPerSec); }
    [[nodiscard]] float Speed() const noexcept { return speed_; }

    // ---- 规划 ----
    // 从 start 到 goal 重新寻路；成功则重置沿路进度并定位到 start 格中心。
    // 失败（网格为空 / 不可达）返回 false，且清空路径状态。
    bool Plan(Cell start, Cell goal)
    {
        hasPath_ = false;
        arrived_ = false;
        path_ = PathResult{};
        if (!grid_)
            return false;
        path_ = grid_->FindPath(start, goal);
        if (!path_.found || path_.path.empty())
            return false;
        hasPath_ = true;
        currentCell_ = path_.path.front();
        position_ = CellCenter(currentCell_);
        if (path_.path.size() == 1)
        {
            // 起点即终点：直接抵达
            targetCell_ = currentCell_;
            segIndex_ = 1;
            arrived_ = true;
            return true;
        }
        segIndex_ = 1;
        targetCell_ = path_.path[1];
        return true;
    }

    // 巡逻点（环形队列）。假定代理当前位于 points[0]，下一次 PlanToNext() 前往 points[1]，
    // 之后依次 points[2] ... points[n-1] -> points[0] -> points[1] ... 闭环。
    void SetPatrolPoints(std::vector<Cell> points)
    {
        patrolPoints_ = std::move(points);
        patrolIndex_ = 0;
    }
    [[nodiscard]] const std::vector<Cell>& PatrolPoints() const noexcept { return patrolPoints_; }

    // 规划到下一个巡逻点（环形）。返回是否找到路径。
    bool PlanToNext()
    {
        if (patrolPoints_.empty())
            return false;
        const size_t n = patrolPoints_.size();
        const size_t next = (static_cast<size_t>(patrolIndex_) + 1) % n;
        patrolIndex_ = static_cast<int>(next);
        const Cell goal = patrolPoints_[patrolIndex_];
        return Plan(currentCell_, goal);
    }

    // ---- 查询 ----
    [[nodiscard]] bool HasPath() const noexcept { return hasPath_ && path_.found && !path_.path.empty(); }
    [[nodiscard]] const PathResult& CurrentPath() const noexcept { return path_; }
    [[nodiscard]] bool Arrived() const noexcept { return arrived_; }
    [[nodiscard]] glm::vec3 Position() const noexcept { return position_; }
    [[nodiscard]] Cell CurrentCell() const noexcept { return currentCell_; }
    [[nodiscard]] Cell TargetCell() const noexcept { return targetCell_; }
    [[nodiscard]] int PatrolIndex() const noexcept { return patrolIndex_; }

    // 当前朝向调试线 + 代理位置标记（金黄），编辑器可复用 NavGrid::DebugLine 绘制。
    [[nodiscard]] std::vector<NavGrid::DebugLine> GetDebugLines() const
    {
        std::vector<NavGrid::DebugLine> lines;
        if (!HasPath())
            return lines;
        const glm::vec3 pos = position_;
        const glm::vec3 tgt = CellCenter(targetCell_);
        const glm::vec3 col(1.0f, 0.9f, 0.2f); // 金黄：代理行迹
        lines.push_back(NavGrid::DebugLine{pos, tgt, col});
        const float s = cellSize_ * 0.3f;
        lines.push_back(
            NavGrid::DebugLine{glm::vec3(pos.x - s, 0.06f, pos.z), glm::vec3(pos.x + s, 0.06f, pos.z), col});
        lines.push_back(
            NavGrid::DebugLine{glm::vec3(pos.x, 0.06f, pos.z - s), glm::vec3(pos.x, 0.06f, pos.z + s), col});
        return lines;
    }

    // 格中心 -> 世界坐标（与 NavGrid::GetDebugLines 同一映射）。
    [[nodiscard]] static glm::vec3 CellToWorld(Cell c, float cellSize, const glm::vec2& origin) noexcept
    {
        return glm::vec3(origin.x + (static_cast<float>(c.x) + 0.5f) * cellSize, 0.0f,
                         origin.y + (static_cast<float>(c.y) + 0.5f) * cellSize);
    }

    // ---- 每帧推进 ----
    // 沿路径从 currentCell 朝 targetCell 移动；单帧可跨多格（循环消费剩余距离）。
    // 抵达路径终点（最后格中心）时置 arrived_=true 并停止。
    void Step(float dt, float cellSize, const glm::vec2& origin)
    {
        cellSize_ = cellSize;
        origin_ = origin;
        if (!HasPath() || arrived_)
            return;
        if (path_.path.empty())
            return;

        float remaining = speed_ * cellSize * dt;
        while (remaining > 0.0f && !arrived_)
        {
            const glm::vec3 target = CellCenter(targetCell_);
            const glm::vec3 toTarget = target - position_;
            const float dist = glm::length(toTarget);
            if (dist < 1e-5f)
            {
                // 已落在目标格中心：直接前进到下一格
                currentCell_ = targetCell_;
                AdvanceSegment();
                continue;
            }
            if (remaining >= dist)
            {
                position_ = target;
                currentCell_ = targetCell_;
                remaining -= dist;
                AdvanceSegment();
            }
            else
            {
                position_ += (toTarget / dist) * remaining;
                remaining = 0.0f;
            }
        }
    }

  private:
    [[nodiscard]] glm::vec3 CellCenter(Cell c) const noexcept { return CellToWorld(c, cellSize_, origin_); }

    void AdvanceSegment()
    {
        ++segIndex_;
        if (segIndex_ >= static_cast<int>(path_.path.size()))
            arrived_ = true;
        else
            targetCell_ = path_.path[static_cast<size_t>(segIndex_)];
    }

    const NavGrid* grid_ = nullptr;
    PathResult path_{};
    bool hasPath_ = false;
    bool arrived_ = false;
    float speed_ = 2.0f; // 格/秒
    float cellSize_ = 1.0f;
    glm::vec2 origin_{0.0f};
    glm::vec3 position_{0.0f};
    Cell currentCell_{0, 0};
    Cell targetCell_{0, 0};
    int segIndex_ = 0; // 当前正前往的目标格在 path_ 中的下标
    std::vector<Cell> patrolPoints_;
    int patrolIndex_ = 0;
};

} // namespace BigHero::Game

