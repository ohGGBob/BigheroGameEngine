#include "app/systems/NavHost.h"

#include "core/Log.h"
#include "scene/EcsScene.h"

namespace BigHero
{
namespace
{
// 单位立方体（中心在原点，边长 1，底面 y=-0.5 / 顶面 y=+0.5）的 8 个角点。
// 与 SceneObject 的 meshId==0 共享立方体网格一致。
const glm::vec3 kCubeCorners[8] = {
    {-0.5f, -0.5f, -0.5f}, // 0
    {0.5f, -0.5f, -0.5f},  // 1
    {0.5f, -0.5f, 0.5f},   // 2
    {-0.5f, -0.5f, 0.5f},  // 3
    {-0.5f, 0.5f, -0.5f},  // 4
    {0.5f, 0.5f, -0.5f},   // 5
    {0.5f, 0.5f, 0.5f},    // 6
    {-0.5f, 0.5f, 0.5f},   // 7
};
// 12 个三角形（6 面 × 2），索引进 kCubeCorners。绕序不影响坡度过滤（取法线绝对值判定）。
const uint8_t kCubeTri[12][3] = {
    {0, 1, 2}, {0, 2, 3}, // 底（y=-0.5，法线 -Y，坡度过滤剔除）
    {4, 6, 5}, {4, 7, 6}, // 顶（y=+0.5，法线 +Y，可行走）
    {0, 5, 1}, {0, 4, 5}, // 前（z=-0.5，竖直面，坡度过滤剔除）
    {2, 7, 3}, {2, 6, 7}, // 后（z=+0.5，竖直面，坡度过滤剔除）
    {3, 4, 0}, {3, 7, 4}, // 左（x=-0.5，竖直面，坡度过滤剔除）
    {1, 6, 2}, {1, 5, 6}, // 右（x=+0.5，竖直面，坡度过滤剔除）
};
} // namespace
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

bool NavHost::BuildFromEcsScene(const Scene::EcsScene& ecsScene, const Navigation::NavBuildSettings& settings)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> idx;
    uint32_t cubeObjects = 0;
    uint32_t skippedObjects = 0;

    // 遍历稳定序渲染实体，按 meshId 提取世界空间三角形。
    // ForEachRenderableWorld 已级联父子矩阵，world 即实体的世界模型矩阵（平移+旋转+缩放+自转）。
    ecsScene.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform& t, const Scene::ecs::Renderable& r, const Scene::ecs::Spin& s,
            const glm::mat4& world)
        {
            (void)t;
            (void)s;
            if (r.meshId != 0u)
            {
                ++skippedObjects; // torus(1)/glTF(2)：CPU 端顶点数据不可直接访问，暂跳过
                return;
            }
            // 单位立方体 8 角点经世界矩阵变换到世界空间。
            glm::vec3 corner[8];
            for (int i = 0; i < 8; ++i)
                corner[i] = glm::vec3(world * glm::vec4(kCubeCorners[i], 1.0f));
            for (const auto& tri : kCubeTri)
            {
                const uint32_t base = static_cast<uint32_t>(verts.size());
                verts.push_back(corner[tri[0]]);
                verts.push_back(corner[tri[1]]);
                verts.push_back(corner[tri[2]]);
                idx.push_back(base);
                idx.push_back(base + 1);
                idx.push_back(base + 2);
            }
            ++cubeObjects;
        });

    if (verts.empty())
    {
        // 空场景（无立方体几何）：清空网格，PolyCount()==0，按契约返回成功。
        navMesh.Clear();
        LOG_INFO("NavMesh 从 ECS 场景构建: 无可用几何（立方体=" << cubeObjects << " 跳过非立方体="
                                                              << skippedObjects << "），PolyCount=0");
        return true;
    }

    navMesh.Build(verts.data(), verts.size(), idx.data(), idx.size(), settings);
    LOG_INFO("NavMesh 从 ECS 场景构建: 立方体对象=" << cubeObjects << " 跳过非立方体=" << skippedObjects
              << " 三角形=" << (idx.size() / 3u) << " polys=" << navMesh.PolyCount());
    return true;
}
} // namespace BigHero
