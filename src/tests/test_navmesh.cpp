// 导航网格（navigation/NavMesh.h）单元测试：纯逻辑、零 GPU，可离线运行。
// 覆盖体素化 / 坡度与净空过滤 / agent 半径膨胀 / 连通域 / 轮廓孔洞 / 最近点投影 / A* + 漏斗拉直。
#include "framework/test_common.h"
#include <cstdio>
#include "navigation/NavMesh.h"

using BigHero::Navigation::NavBuildSettings;
using BigHero::Navigation::NavMesh;
using BigHero::Navigation::NavMeshQuery;

namespace
{
// 简易网格构造器：顶点 + 索引，全部世界坐标。
struct MeshData
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> idx;

    uint32_t AddVert(const glm::vec3& v)
    {
        verts.push_back(v);
        return static_cast<uint32_t>(verts.size() - 1);
    }
    void AddTri(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        idx.push_back(AddVert(a));
        idx.push_back(AddVert(b));
        idx.push_back(AddVert(c));
    }
    // 四边形（a→b→c→d 逆时针）拆成两个三角面。
    void AddQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
    {
        AddTri(a, b, c);
        AddTri(a, c, d);
    }
    // 轴对齐立方体（6 个面，含顶盖）。
    void AddBox(const glm::vec3& lo, const glm::vec3& hi)
    {
        const float x0 = lo.x, y0 = lo.y, z0 = lo.z;
        const float x1 = hi.x, y1 = hi.y, z1 = hi.z;
        AddQuad({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}); // 顶
        AddQuad({x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}); // 底
        AddQuad({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}); // -Z
        AddQuad({x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}); // +Z
        AddQuad({x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}); // -X
        AddQuad({x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}); // +X
    }
    bool Build(NavMesh& nav, const NavBuildSettings& s) const
    {
        return nav.Build(verts.data(), verts.size(), idx.data(), idx.size(), s);
    }
};

// 10×10 的水平地面（y = 0）。
MeshData FlatFloor(float size = 10.0f)
{
    MeshData m;
    m.AddQuad({0, 0, 0}, {size, 0, 0}, {size, 0, size}, {0, 0, size});
    return m;
}

const glm::vec3 kExtents(1.0f, 2.0f, 1.0f);
} // namespace

TEST_CASE("Nav.EmptyInputRejected")
{
    NavMesh nav;
    NavBuildSettings s;
    CHECK(!nav.Build(nullptr, 0, nullptr, 0, s));
    CHECK(nav.IsEmpty());
    CHECK_EQ(nav.PolyCount(), 0);

    const std::vector<uint32_t> idx = {0, 1, 2};
    const std::vector<glm::vec3> verts = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    CHECK(!nav.Build(verts.data(), verts.size(), nullptr, idx.size(), s)); // 缺索引
    CHECK(!nav.Build(verts.data(), verts.size(), idx.data(), 0, s));       // 索引不足 3
}

TEST_CASE("Nav.FlatFloorBake")
{
    const MeshData m = FlatFloor();
    NavMesh nav;
    CHECK(m.Build(nav, NavBuildSettings{}));
    CHECK(!nav.IsEmpty());
    CHECK_GT(nav.PolyCount(), 0);
    CHECK_EQ(nav.GetStats().regions, 1);

    // 10m×10m 地面，每边被 agent 半径（2 体素 = 0.5m）吃掉 → 理论 9×9 = 81 m²。
    CHECK_GT(nav.TotalArea(), 76.0f);
    CHECK_LT(nav.TotalArea(), 86.0f);

    // 面积应等于可行走区域，且所有顶点都在原地面范围内。
    for (int i = 0; i < nav.VertexCount(); ++i)
    {
        const glm::vec3 v = nav.Vertex(i);
        CHECK_GE(v.x, 0.0f);
        CHECK_LE(v.x, 10.0f);
        CHECK_GE(v.z, 0.0f);
        CHECK_LE(v.z, 10.0f);
        CHECK_NEAR(v.y, 0.0f, 1e-3f);
    }
}

TEST_CASE("Nav.ErosionRespectsAgentRadius")
{
    const MeshData m = FlatFloor();
    NavMesh thin;
    NavBuildSettings s0;
    s0.walkableRadius = 0;
    CHECK(m.Build(thin, s0));
    const float area0 = thin.TotalArea();

    NavMesh thick;
    NavBuildSettings s4;
    s4.walkableRadius = 4; // 1.0m
    CHECK(m.Build(thick, s4));
    const float area4 = thick.TotalArea();

    // 半径越大，可行走面积越小；4 体素 → 每边吃 1m → 8×8 = 64 m²。
    CHECK_LT(area4, area0);
    CHECK_GT(area4, 58.0f);
    CHECK_LT(area4, 70.0f);
}

TEST_CASE("Nav.ObstacleCreatesHole")
{
    MeshData m = FlatFloor();
    m.AddBox({4.0f, 0.0f, 4.0f}, {6.0f, 1.0f, 6.0f});
    NavMesh nav;
    NavBuildSettings s;
    s.walkableRadius = 1;
    CHECK(m.Build(nav, s));
    CHECK(!nav.IsEmpty());

    // 地面被挖出一个洞（外环 + 孔洞环），且箱体顶面本身成为一个独立连通域。
    CHECK_GE(nav.GetStats().contourLoops, 2);
    CHECK_GE(nav.GetStats().regions, 2);

    // 洞中心 (5,0,5) 处地面已被挖掉。箱体顶面 (y=1) 是独立连通域且在 extents.y 范围内，
    // 因此可能吸附到箱顶；但绝不应吸附到地面 (y=0)。
    NavMeshQuery q(nav);
    const NavMeshQuery::NearestPointResult inside = q.FindNearestPoint(glm::vec3(5.0f, 0.0f, 5.0f), kExtents);
    CHECK(!inside.found || inside.point.y > 0.5f);
}

TEST_CASE("Nav.PathStraightOnOpenFloor")
{
    const MeshData m = FlatFloor();
    NavMesh nav;
    CHECK(m.Build(nav, NavBuildSettings{}));
    NavMeshQuery q(nav);

    const NavMeshQuery::Path p = q.FindPath({2.0f, 0.0f, 2.0f}, {8.0f, 0.0f, 8.0f}, kExtents);
    CHECK(p.found);
    CHECK_GE(p.points.size(), 2u);
    // 空旷地面上的直线：漏斗拉直后不应产生多余的拐点。
    CHECK_LE(p.points.size(), 2u);
    CHECK_NEAR(p.length, std::sqrt(36.0f + 36.0f), 0.05f);
}

TEST_CASE("Nav.PathAroundObstacle")
{
    MeshData m = FlatFloor();
    m.AddBox({4.0f, 0.0f, 4.0f}, {6.0f, 1.0f, 6.0f});
    NavMesh nav;
    NavBuildSettings s;
    s.walkableRadius = 1;
    CHECK(m.Build(nav, s));
    NavMeshQuery q(nav);

    const NavMeshQuery::Path p = q.FindPath({1.0f, 0.0f, 5.0f}, {9.0f, 0.0f, 5.0f}, kExtents);
    CHECK(p.found);
    CHECK_GE(p.points.size(), 3u); // 至少要绕一次
    CHECK_GT(p.length, 8.0f);      // 必然长于直线距离

    // 任何路点都不得落在障碍物（含膨胀余量）内部。
    for (const glm::vec3& v : p.points)
    {
        const bool inX = v.x > 3.4f && v.x < 6.6f;
        const bool inZ = v.z > 3.4f && v.z < 6.6f;
        CHECK(!(inX && inZ));
    }
}

TEST_CASE("Nav.DisconnectedRegionsHaveNoPath")
{
    MeshData m;
    m.AddQuad({0, 0, 0}, {4, 0, 0}, {4, 0, 4}, {0, 0, 4});
    m.AddQuad({10, 0, 0}, {14, 0, 0}, {14, 0, 4}, {10, 0, 4});
    NavMesh nav;
    CHECK(m.Build(nav, NavBuildSettings{}));
    CHECK_EQ(nav.GetStats().regions, 2);

    NavMeshQuery q(nav);
    const NavMeshQuery::Path p = q.FindPath({2.0f, 0.0f, 2.0f}, {12.0f, 0.0f, 2.0f}, kExtents);
    CHECK(!p.found);
}

TEST_CASE("Nav.SteepSlopeRejected")
{
    // 坡度 63°（水平 2m、抬升 4m），远超默认 45° → 不该产生可行走面。
    MeshData m;
    m.AddQuad({0, 0, 0}, {2, 4, 0}, {2, 4, 4}, {0, 0, 4});
    NavMesh nav;
    CHECK(m.Build(nav, NavBuildSettings{}));
    CHECK(nav.IsEmpty());
    CHECK_EQ(nav.PolyCount(), 0);
}

TEST_CASE("Nav.GentleSlopeAccepted")
{
    // 坡度约 26.6°（水平 4m、抬升 2m）→ 可行走。
    MeshData m;
    m.AddQuad({0, 0, 0}, {4, 2, 0}, {4, 2, 4}, {0, 0, 4});
    NavMesh nav;
    NavBuildSettings s;
    s.walkableRadius = 1;
    CHECK(m.Build(nav, s));
    CHECK(!nav.IsEmpty());
    CHECK_GT(nav.TotalArea(), 5.0f);

    // 导航面高度应随坡度抬升，而不是被压平成 0。
    float minY = 1e9f;
    float maxY = -1e9f;
    for (int i = 0; i < nav.VertexCount(); ++i)
    {
        minY = std::min(minY, nav.Vertex(i).y);
        maxY = std::max(maxY, nav.Vertex(i).y);
    }
    CHECK_GT(maxY - minY, 0.8f);
}

TEST_CASE("Nav.StepWithinClimbIsConnected")
{
    // 0.4m 台阶（= 2 体素 = walkableClimb 上限）：下阶与上阶应连通。
    MeshData m;
    m.AddQuad({0, 0, 0}, {5, 0, 0}, {5, 0, 10}, {0, 0, 10});               // 下阶
    m.AddQuad({5, 0.4f, 0}, {10, 0.4f, 0}, {10, 0.4f, 10}, {5, 0.4f, 10}); // 上阶
    NavMesh nav;
    NavBuildSettings s;
    s.walkableRadius = 1;
    CHECK(m.Build(nav, s));
    CHECK_EQ(nav.GetStats().regions, 1);

    NavMeshQuery q(nav);
    const NavMeshQuery::Path p = q.FindPath({2.0f, 0.0f, 5.0f}, {8.0f, 0.4f, 5.0f}, kExtents);
    CHECK(p.found);
}

TEST_CASE("Nav.StepBeyondClimbIsSplit")
{
    // 1.0m 台阶（= 5 体素，远超 climb=2）→ 上下两层互不连通。
    MeshData m;
    m.AddQuad({0, 0, 0}, {5, 0, 0}, {5, 0, 10}, {0, 0, 10});
    m.AddQuad({5, 1.0f, 0}, {10, 1.0f, 0}, {10, 1.0f, 10}, {5, 1.0f, 10});
    NavMesh nav;
    NavBuildSettings s;
    s.walkableRadius = 1;
    CHECK(m.Build(nav, s));
    CHECK_EQ(nav.GetStats().regions, 2);

    NavMeshQuery q(nav);
    const NavMeshQuery::Path p = q.FindPath({2.0f, 0.0f, 5.0f}, {8.0f, 1.0f, 5.0f}, kExtents);
    CHECK(!p.found);
}

TEST_CASE("Nav.BridgeCreatesTwoLayers")
{
    // 地面被一道沟切断，上方有桥 → 桥面与地面应属不同连通域（多层导航）。
    MeshData m;
    m.AddQuad({0, 0, 0}, {4, 0, 0}, {4, 0, 10}, {0, 0, 10});
    m.AddQuad({6, 0, 0}, {10, 0, 0}, {10, 0, 10}, {6, 0, 10});
    m.AddQuad({3.5f, 2.0f, 4.0f}, {6.5f, 2.0f, 4.0f}, {6.5f, 2.0f, 6.0f}, {3.5f, 2.0f, 6.0f}); // 桥面
    NavMesh nav;
    NavBuildSettings s;
    s.walkableRadius = 1;
    CHECK(m.Build(nav, s));
    CHECK_GE(nav.GetStats().regions, 2);
}

TEST_CASE("Nav.NearestPointProjectsOntoSurface")
{
    const MeshData m = FlatFloor();
    NavMesh nav;
    CHECK(m.Build(nav, NavBuildSettings{}));
    NavMeshQuery q(nav);

    const NavMeshQuery::NearestPointResult r = q.FindNearestPoint(glm::vec3(5.0f, 0.0f, 5.0f), kExtents);
    CHECK(r.found);
    CHECK_GE(r.poly, 0);
    CHECK_LT(r.poly, nav.PolyCount());
    CHECK_NEAR(r.point.x, 5.0f, 1e-3f);
    CHECK_NEAR(r.point.z, 5.0f, 1e-3f);
    CHECK_NEAR(r.point.y, 0.0f, 1e-3f);
    CHECK_NEAR(r.distance, 0.0f, 1e-3f);

    // 远离导航网格（横向 20m 外）时应该找不到。
    const NavMeshQuery::NearestPointResult far =
        q.FindNearestPoint(glm::vec3(30.0f, 0.0f, 5.0f), glm::vec3(0.5f, 2.0f, 0.5f));
    CHECK(!far.found);
}

TEST_CASE("Nav.VerticalExtentFiltersOtherFloors")
{
    const MeshData m = FlatFloor();
    NavMesh nav;
    CHECK(m.Build(nav, NavBuildSettings{}));
    NavMeshQuery q(nav);

    // 站在 5m 高处（远超 extents.y=0.5）→ 不该吸附到地面导航面。
    const NavMeshQuery::NearestPointResult high =
        q.FindNearestPoint(glm::vec3(5.0f, 5.0f, 5.0f), glm::vec3(1.0f, 0.5f, 1.0f));
    CHECK(!high.found);
}

TEST_CASE("Nav.PolyAdjacencyIsSymmetric")
{
    const MeshData m = FlatFloor();
    NavMesh nav;
    NavBuildSettings s;
    s.maxEdgeLength = 3; // 强制细分，制造更多三角形与邻接关系
    CHECK(m.Build(nav, s));
    CHECK_GT(nav.PolyCount(), 1);

    int neighborCount = 0;
    for (int i = 0; i < nav.PolyCount(); ++i)
    {
        const NavMesh::Poly& p = nav.PolyAt(i);
        for (int e = 0; e < 3; ++e)
        {
            const int nb = p.n[e];
            if (nb < 0)
            {
                continue;
            }
            ++neighborCount;
            // 邻接必须对称：nb 的某条边也应指向 i。
            bool back = false;
            for (int k = 0; k < 3; ++k)
            {
                if (nav.PolyAt(nb).n[k] == i)
                {
                    back = true;
                }
            }
            CHECK(back);
        }
    }
    CHECK_GT(neighborCount, 0);
}

TEST_CASE("Nav.ClearResetsState")
{
    const MeshData m = FlatFloor();
    NavMesh nav;
    CHECK(m.Build(nav, NavBuildSettings{}));
    CHECK(!nav.IsEmpty());

    nav.Clear();
    CHECK(nav.IsEmpty());
    CHECK_EQ(nav.PolyCount(), 0);
    CHECK_EQ(nav.VertexCount(), 0);
    CHECK_EQ(nav.GetStats().spans, 0);
}
