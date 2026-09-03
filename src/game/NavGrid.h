#pragma once
// 导航网格 + A* 寻路（纯 CPU、仅标准库 + glm，无 GPU/窗口依赖，可离线单测）。
//
// 设计：
//   - NavGrid：规则二维网格，每格可标记为"阻挡"。支持动态增删障碍、尺寸重置。
//   - 寻路：A*（启发式 + 开放集）。启发式可选曼哈顿 / 欧氏 / 八向(Octile)。
//     4 邻接（默认）或 8 邻接（含对角）。对角移动禁止"切角"（两侧正交格均阻挡时不可穿）。
//   - 路径：从起点到终点的格子序列（含起点与终点）；不可达时返回空向量。
//   - 可视化：GetDebugLines() 返回网格线 + 路径线（供编辑器 ImGui 调试线复用）。
//
// 该模块是"玩法+工具一轮"升级 17-1 的核心，后续可挂接 AI 巡逻 / 角色自动寻路。

#include <cmath>
#include <limits>
#include <queue>
#include <vector>

#include <glm/glm.hpp>

namespace BigHero::Game
{
// 网格坐标（整数格索引，原点在左上，x 向右、y 向下）。
struct Cell
{
    int x = 0;
    int y = 0;

    bool operator==(const Cell& o) const noexcept { return x == o.x && y == o.y; }
    bool operator!=(const Cell& o) const noexcept { return !(*this == o); }
};

// 启发式类型
enum class NavHeuristic : int
{
    Manhattan = 0, // |dx|+|dy|（4 邻接最优）
    Euclidean = 1, // sqrt(dx^2+dy^2)
    Octile = 2     // 八向距离（8 邻接最优）
};

// 寻路结果附加信息（诊断 / 编辑器展示）
struct PathResult
{
    std::vector<Cell> path; // 格子序列（含起点与终点）；空 = 不可达
    int nodesExpanded = 0;  // 被展开（出开放集）的节点数
    bool found = false;
};

class NavGrid
{
  public:
    NavGrid() = default;
    explicit NavGrid(int width, int height, bool allowDiagonal = false) { Resize(width, height, allowDiagonal); }

    // 重置网格尺寸，清空所有障碍与寻路状态。
    void Resize(int width, int height, bool allowDiagonal = false)
    {
        width_ = std::max(1, width);
        height_ = std::max(1, height);
        allowDiagonal_ = allowDiagonal;
        blocked_.assign(static_cast<size_t>(width_ * height_), false);
    }

    [[nodiscard]] int Width() const noexcept { return width_; }
    [[nodiscard]] int Height() const noexcept { return height_; }
    [[nodiscard]] int CellCount() const noexcept { return width_ * height_; }
    [[nodiscard]] bool AllowDiagonal() const noexcept { return allowDiagonal_; }

    // 坐标合法性
    [[nodiscard]] bool InBounds(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    [[nodiscard]] bool InBounds(Cell c) const noexcept { return InBounds(c.x, c.y); }

    // 阻挡标记
    void SetBlocked(int x, int y, bool blocked = true)
    {
        if (InBounds(x, y))
            blocked_[static_cast<size_t>(y * width_ + x)] = blocked;
    }
    void SetBlocked(Cell c, bool blocked = true) { SetBlocked(c.x, c.y, blocked); }
    [[nodiscard]] bool IsBlocked(int x, int y) const noexcept
    {
        return InBounds(x, y) ? blocked_[static_cast<size_t>(y * width_ + x)] : true;
    }
    [[nodiscard]] bool IsBlocked(Cell c) const noexcept { return IsBlocked(c.x, c.y); }

    // 清空全部障碍
    void Clear() { std::fill(blocked_.begin(), blocked_.end(), false); }

    // 设置启发式（默认曼哈顿，配合 4 邻接）
    void SetHeuristic(NavHeuristic h) noexcept { heuristic_ = h; }
    [[nodiscard]] NavHeuristic GetHeuristic() const noexcept { return heuristic_; }

    // ---- A* 寻路 ----
    // start 或 goal 越界 / 自身被阻挡：返回空（found=false）。
    // 起点即终点：返回仅含该格的路径（found=true）。
    [[nodiscard]] PathResult FindPath(Cell start, Cell goal) const
    {
        PathResult res;
        if (!InBounds(start) || !InBounds(goal))
            return res;
        if (IsBlocked(start) || IsBlocked(goal))
            return res;
        if (start == goal)
        {
            res.path.push_back(start);
            res.found = true;
            return res;
        }

        const int n = CellCount();
        std::vector<float> g(static_cast<size_t>(n), std::numeric_limits<float>::infinity());
        std::vector<int> cameFrom(static_cast<size_t>(n), -1);
        std::vector<uint8_t> closed(static_cast<size_t>(n), 0);

        const int startIdx = ToIndex(start);
        const int goalIdx = ToIndex(goal);
        g[static_cast<size_t>(startIdx)] = 0.0f;

        // 最小堆（按 f = g + h）
        struct OpenNode
        {
            int idx;
            float f;
        };
        auto cmp = [](const OpenNode& a, const OpenNode& b) { return a.f > b.f; };
        std::priority_queue<OpenNode, std::vector<OpenNode>, decltype(cmp)> open(cmp);
        open.push(OpenNode{startIdx, Heuristic(FromIndex(startIdx), goal)});

        while (!open.empty())
        {
            const OpenNode cur = open.top();
            open.pop();
            const int ci = cur.idx;
            if (closed[static_cast<size_t>(ci)])
                continue; // 过期堆项（已被更优路径松弛）
            closed[static_cast<size_t>(ci)] = 1;
            ++res.nodesExpanded;

            if (ci == goalIdx)
            {
                // 回溯路径
                std::vector<int> rev;
                for (int p = goalIdx; p != -1; p = cameFrom[static_cast<size_t>(p)])
                    rev.push_back(p);
                res.path.resize(rev.size());
                for (size_t i = 0; i < rev.size(); ++i)
                    res.path[i] = FromIndex(rev[rev.size() - 1 - i]);
                res.found = true;
                return res;
            }

            const Cell cc = FromIndex(ci);
            for (int d = 0; d < (allowDiagonal_ ? 8 : 4); ++d)
            {
                const int nx = cc.x + kDx[d];
                const int ny = cc.y + kDy[d];
                if (!InBounds(nx, ny))
                    continue;
                const int ni = ny * width_ + nx;
                if (closed[static_cast<size_t>(ni)])
                    continue;
                if (IsBlocked(nx, ny))
                    continue;
                // 对角切角防护：两侧正交格均阻挡时禁止斜穿
                if (d >= 4)
                {
                    const int ox = cc.x + kDx[d - 4];
                    const int oy = cc.y + kDy[d - 4];
                    const int ax = cc.x + kDx[(d - 4 + 1) % 4];
                    const int ay = cc.y + kDy[(d - 4 + 1) % 4];
                    if (IsBlocked(ox, oy) && IsBlocked(ax, ay))
                        continue;
                }

                const float step = (d >= 4) ? kDiagCost : 1.0f;
                const float tentative = g[static_cast<size_t>(ci)] + step;
                if (tentative < g[static_cast<size_t>(ni)])
                {
                    g[static_cast<size_t>(ni)] = tentative;
                    cameFrom[static_cast<size_t>(ni)] = ci;
                    const Cell nc{nx, ny};
                    open.push(OpenNode{ni, tentative + Heuristic(nc, goal)});
                }
            }
        }
        return res; // 开放集耗尽，未达终点 -> 不可达
    }

    // 便捷重载：直接用整数坐标
    [[nodiscard]] PathResult FindPath(int sx, int sy, int gx, int gy) const
    {
        return FindPath(Cell{sx, sy}, Cell{gx, gy});
    }

    // ---- 可视化调试线（世界平面 y=0，xy 映射到世界 x/z；cellSize 决定格宽，origin 为网格左下角世界坐标） ----
    struct DebugLine
    {
        glm::vec3 a;
        glm::vec3 b;
        glm::vec3 color;
    };
    [[nodiscard]] std::vector<DebugLine> GetDebugLines(float cellSize = 1.0f, const glm::vec2& origin = glm::vec2(0.0f),
                                                       const PathResult* path = nullptr) const
    {
        std::vector<DebugLine> lines;
        const glm::vec3 gridCol(0.25f, 0.55f, 0.85f);
        const glm::vec3 blockCol(0.85f, 0.25f, 0.25f);
        const glm::vec3 pathCol(0.3f, 0.95f, 0.45f);

        // 横向与纵向网格线
        for (int x = 0; x <= width_; ++x)
        {
            const float wx = origin.x + static_cast<float>(x) * cellSize;
            lines.push_back(DebugLine{glm::vec3(wx, 0.02f, origin.y),
                                      glm::vec3(wx, 0.02f, origin.y + static_cast<float>(height_) * cellSize),
                                      gridCol});
        }
        for (int y = 0; y <= height_; ++y)
        {
            const float wz = origin.y + static_cast<float>(y) * cellSize;
            lines.push_back(DebugLine{glm::vec3(origin.x, 0.02f, wz),
                                      glm::vec3(origin.x + static_cast<float>(width_) * cellSize, 0.02f, wz), gridCol});
        }
        // 障碍格：中心叉线
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
                if (IsBlocked(x, y))
                {
                    const float cx = origin.x + (static_cast<float>(x) + 0.5f) * cellSize;
                    const float cz = origin.y + (static_cast<float>(y) + 0.5f) * cellSize;
                    const float h = cellSize * 0.4f;
                    lines.push_back(
                        DebugLine{glm::vec3(cx - h, 0.03f, cz - h), glm::vec3(cx + h, 0.03f, cz + h), blockCol});
                    lines.push_back(
                        DebugLine{glm::vec3(cx - h, 0.03f, cz + h), glm::vec3(cx + h, 0.03f, cz - h), blockCol});
                }

        // 路径：相邻格中心连线
        if (path && path->found && path->path.size() >= 2)
        {
            for (size_t i = 1; i < path->path.size(); ++i)
            {
                const Cell a = path->path[i - 1];
                const Cell b = path->path[i];
                const glm::vec3 pa(origin.x + (static_cast<float>(a.x) + 0.5f) * cellSize, 0.05f,
                                   origin.y + (static_cast<float>(a.y) + 0.5f) * cellSize);
                const glm::vec3 pb(origin.x + (static_cast<float>(b.x) + 0.5f) * cellSize, 0.05f,
                                   origin.y + (static_cast<float>(b.y) + 0.5f) * cellSize);
                lines.push_back(DebugLine{pa, pb, pathCol});
            }
        }
        return lines;
    }

  private:
    [[nodiscard]] int ToIndex(Cell c) const noexcept { return c.y * width_ + c.x; }
    [[nodiscard]] Cell FromIndex(int i) const noexcept { return Cell{i % width_, i / width_}; }

    [[nodiscard]] float Heuristic(Cell a, Cell goal) const noexcept
    {
        const float dx = static_cast<float>(std::abs(a.x - goal.x));
        const float dy = static_cast<float>(std::abs(a.y - goal.y));
        switch (heuristic_)
        {
        case NavHeuristic::Euclidean:
            return std::sqrt(dx * dx + dy * dy);
        case NavHeuristic::Octile:
        {
            const float diag = std::min(dx, dy);
            return (dx + dy) + (kDiagCost - 2.0f) * diag;
        }
        case NavHeuristic::Manhattan:
        default:
            return dx + dy;
        }
    }

    static constexpr int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static constexpr float kDiagCost = 1.41421356f; // sqrt(2)

    int width_ = 0;
    int height_ = 0;
    bool allowDiagonal_ = false;
    NavHeuristic heuristic_ = NavHeuristic::Manhattan;
    std::vector<uint8_t> blocked_;
};

} // namespace BigHero::Game
