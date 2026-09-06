#pragma once
// 导航系统：A* 网格寻路 + NavAgent 巡逻代理
// 原 Application 中 navGrid_, navAgent_, navPath_, UpdateNavPath, UpdateNavAgent 等逻辑

#include "ISubSystem.h"
#include "game/NavGrid.h"
#include "game/NavAgent.h"
#include "scene/Scene.h"
#include <vector>

namespace BigHero::App
{

class NavigationSystem final : public ISubSystem
{
public:
    NavigationSystem() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "NavigationSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -15; }

    void Init()
    {
        navGrid_ = Game::NavGrid(32, 32, 1.0f, {-8.0f, -8.0f});
        navAgent_ = Game::NavAgent(navGrid_, {0, 0});
    }

    void Shutdown() override {}

    void Update(const FrameContext& frame) override
    {
        if (!agentEnabled_)
            return;
        navAgent_.Step(frame.deltaTime, cellSize_, origin_);
        if (navAgent_.Arrived())
            navAgent_.PlanToNext();
    }

    void PreRender(uint32_t) override {}
    void OnSwapchainRecreated() override {}
    void OnRenderPassRecreated() override {}

    void RecalculatePath(int startX, int startY, int goalX, int goalY)
    {
        navStartX_ = startX; navStartY_ = startY;
        navGoalX_ = goalX; navGoalY_ = goalY;
        navPath_ = navGrid_.FindPath(startX, startY, goalX, goalY);
    }

    void UpdateGridObstacles(const std::vector<Scene::SceneObject>& scene)
    {
        navGrid_.ClearObstacles();
        for (const auto& obj : scene)
        {
            if (obj.meshId != 0)
            {
                const int gx = static_cast<int>((obj.position.x - origin_.x) / cellSize_);
                const int gy = static_cast<int>((obj.position.z - origin_.y) / cellSize_);
                if (navGrid_.InBounds(gx, gy))
                    navGrid_.SetObstacle(gx, gy, true);
            }
        }
    }

    void RecalculatePath()
    {
        navPath_ = navGrid_.FindPath(navStartX_, navStartY_, navGoalX_, navGoalY_);
    }

    void SetEnabled(bool v) { gridEnabled_ = v; }
    [[nodiscard]] bool GridEnabled() const noexcept { return gridEnabled_; }
    void SetAgentEnabled(bool v) { agentEnabled_ = v; }
    [[nodiscard]] bool AgentEnabled() const noexcept { return agentEnabled_; }

    void SetGridParams(int w, int h, float cellSize, const glm::vec2& origin)
    {
        navGrid_ = Game::NavGrid(w, h, cellSize, origin);
        navAgent_ = Game::NavAgent(navGrid_, {0, 0});
        cellSize_ = cellSize;
        origin_ = origin;
    }

    void SetStartGoal(int sx, int sy, int gx, int gy)
    {
        navStartX_ = sx; navStartY_ = sy;
        navGoalX_ = gx; navGoalY_ = gy;
        navPath_ = navGrid_.FindPath(sx, sy, gx, gy);
    }

    [[nodiscard]] const Game::NavGrid& Grid() const noexcept { return navGrid_; }
    [[nodiscard]] const Game::NavAgent& Agent() const noexcept { return navAgent_; }
    [[nodiscard]] const Game::PathResult& Path() const noexcept { return navPath_; }
    [[nodiscard]] bool GridEnabled() const noexcept { return gridEnabled_; }
    [[nodiscard]] bool AgentEnabled() const noexcept { return agentEnabled_; }
    [[nodiscard]] int CellSize() const noexcept { return cellSize_; }
    [[nodiscard]] const glm::vec2& Origin() const noexcept { return origin_; }
    [[nodiscard]] int StartX() const noexcept { return navStartX_; }
    [[nodiscard]] int StartY() const noexcept { return navStartY_; }
    [[nodiscard]] int GoalX() const noexcept { return navGoalX_; }
    [[nodiscard]] int GoalY() const noexcept { return navGoalY_; }

    [[nodiscard]] const char* Name() const noexcept override { return "NavigationSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -15; }

private:
    Game::NavGrid navGrid_;
    Game::NavAgent navAgent_;
    Game::PathResult navPath_;
    int navStartX_ = 1, navStartY_ = 1;
    int navGoalX_ = 14, navGoalY_ = 14;
    float cellSize_ = 1.0f;
    glm::vec2 origin_{-8.0f, -8.0f};
    bool gridEnabled_ = false;
    bool agentEnabled_ = true;
};

} // namespace BigHero::App