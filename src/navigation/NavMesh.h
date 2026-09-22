#pragma once
// 导航网格（NavMesh）：把世界空间的三角形汤烘焙成「可行走凸多边形」，并在其上做 A* + 漏斗拉直寻路（U2-N2）。
// 纯 CPU、仅依赖 glm 与标准库、不触碰 Vulkan；可离线单测。
//
// 背景与动机：
//   商业化引擎里，「AI 会不会走」直接决定游戏能不能出货。角色绕开桌椅、爬坡、过桥、上楼梯
//   这些行为如果靠关卡设计师手写路点，项目规模一大就必然失控。Unity 的 NavMesh 给出的答案是：
//   把场景几何**离线烘焙**成一张多边形网，运行时只在这张小图上做图搜索。
//   本模块就是这条链路的自研实现，流程对齐 Recast/Detour 的经典五段式：
//
//     三角形 → 高度场体素化 → 可走性过滤 → 半径膨胀(erosion) → 连通域 → 轮廓 → 耳切三角化 → 寻路
//
// 设计取舍（与完整 Recast 的差异，均为有意为之）：
//   1. **膨胀距离用 8 邻域 BFS 层级（切比雪夫）**，而非 Recast 的精确欧氏距离场。
//      等价把 agent 半径按外接正方形处理，结果偏保守（窄通道更容易被判为不可走），
//      但实现简单、确定性强、无浮点漂移。若需要精确半径可后续换成 chamfer 距离变换。
//   2. **连通域在 span（体素段）级别做泛洪**，而非 2D 网格级别。这让「桥上 / 桥下」「楼上 / 楼下」
//      能自然分成不同区域，各自出轮廓——即支持多层导航。代价是高度差超过 walkableClimb 的
//      两层不会自动连通（楼梯/坡道本身是连续几何，因此天然连通，不受影响）。
//   3. **轮廓采用「有向边串联」而非 marching squares**：每个区域格子向缺失邻居的一侧
//      发射一条 CCW 有向边，再按端点串成环。外环天然 CCW、孔洞环天然 CW，方向即语义，
//      省掉一次朝向判断。孔洞用「搭桥（bridge）」并入外环后再耳切。
//
// 契约：
//   - Build() 的输入是世界空间三角形（任意顺序、允许重复顶点），不要求索引闭合。
//   - Build() 失败时对象保持 Clear() 状态；成功则 PolyCount() > 0 或为 0（空场景算成功）。
//   - NavMeshQuery 只读取 NavMesh，可多线程并发调用（无内部可变状态）。
//   - 所有多边形顶点绕序统一为 CCW（在 XZ 平面上，叉积 > 0）。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace BigHero::Navigation
{

// 烘焙参数（单位见注释；体素数 = 离散格数，非世界长度）。
struct NavBuildSettings
{
    float cellSize = 0.25f;              // XZ 平面体素边长（米）
    float cellHeight = 0.20f;            // Y 方向体素高度（米），越小垂直精度越高
    float walkableSlopeDegrees = 45.0f;  // 最大可行走坡度
    int walkableHeight = 10;             // 站立净空（体素数），用于剔除「头顶太低」的位置
    int walkableClimb = 2;               // 可跨越台阶高度（体素数）
    int walkableRadius = 2;              // agent 半径（体素数），会把可行走区向内收缩
    float maxSimplificationError = 1.5f; // 轮廓简化容差（体素单位），越大顶点越少
    int maxEdgeLength = 0;               // 轮廓边最大长度（体素单位），0 = 不细分
    int minRegionSpans = 1;              // 小于此体素数的连通域直接丢弃（滤掉碎片）
};

namespace NavDetail
{
// XZ 平面上的 2D 叉积：(b-a) × (c-a) 的「竖直」分量。>0 表示 c 在 a→b 的左侧（CCW）。
inline float Cross2(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
{
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

inline glm::vec2 XZ(const glm::vec3& v)
{
    return glm::vec2(v.x, v.z);
}

inline float SignedArea2(const std::vector<glm::vec2>& loop)
{
    float a = 0.0f;
    for (size_t i = 0, n = loop.size(); i < n; ++i)
    {
        a += Cross2(glm::vec2(0.0f), loop[i], loop[(i + 1) % n]);
    }
    return a * 0.5f;
}

// 点是否在三角形内（含边界，带 epsilon 容差）。用于耳切时的「此耳是否包含其它顶点」判定。
inline bool PointInTriangle2(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
{
    const float e = 1e-5f;
    const float d1 = Cross2(a, b, p);
    const float d2 = Cross2(b, c, p);
    const float d3 = Cross2(c, a, p);
    const bool hasNeg = (d1 < -e) || (d2 < -e) || (d3 < -e);
    const bool hasPos = (d1 > e) || (d2 > e) || (d3 > e);
    return !(hasNeg && hasPos);
}

// 把点 p 投影（钳制）到三角形内部，返回 XZ 平面上最近的点。
inline glm::vec2 ClosestPointOnTriangle2(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
{
    if (PointInTriangle2(p, a, b, c))
    {
        return p;
    }
    auto ClosestOnSeg = [](const glm::vec2& q, const glm::vec2& s0, const glm::vec2& s1)
    {
        const glm::vec2 d = s1 - s0;
        const float len2 = glm::dot(d, d);
        float t = len2 > 1e-12f ? glm::dot(q - s0, d) / len2 : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));
        return s0 + d * t;
    };
    const glm::vec2 c1 = ClosestOnSeg(p, a, b);
    const glm::vec2 c2 = ClosestOnSeg(p, b, c);
    const glm::vec2 c3 = ClosestOnSeg(p, c, a);
    const float d1 = glm::dot(p - c1, p - c1);
    const float d2 = glm::dot(p - c2, p - c2);
    const float d3 = glm::dot(p - c3, p - c3);
    if (d1 <= d2 && d1 <= d3)
    {
        return c1;
    }
    return d2 <= d3 ? c2 : c3;
}

// 三角形重心坐标（XZ 平面），用于把平面高度插值到任意内部点。
inline void Barycentric2(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, float& w0,
                         float& w1, float& w2)
{
    const float v0 = Cross2(a, b, p);
    const float v1 = Cross2(b, c, p);
    const float v2 = Cross2(c, a, p);
    const float sum = v0 + v1 + v2;
    if (std::fabs(sum) < 1e-12f)
    {
        w0 = w1 = w2 = 1.0f / 3.0f;
        return;
    }
    w2 = v0 / sum;
    w0 = v1 / sum;
    w1 = v2 / sum;
}

// 轴对齐矩形与三角形的分离轴（SAT）重叠测试；对退化（面积为 0）的竖直三角形同样成立。
inline bool RectOverlapsTriangle2D(const glm::vec2& rMin, const glm::vec2& rMax, const glm::vec2& t0,
                                   const glm::vec2& t1, const glm::vec2& t2)
{
    const glm::vec2 rect[4] = {rMin, glm::vec2(rMax.x, rMin.y), rMax, glm::vec2(rMin.x, rMax.y)};
    const glm::vec2 tri[3] = {t0, t1, t2};
    const glm::vec2 axes[5] = {glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 1.0f), glm::vec2(-(t1.y - t0.y), t1.x - t0.x),
                               glm::vec2(-(t2.y - t1.y), t2.x - t1.x), glm::vec2(-(t0.y - t2.y), t0.x - t2.x)};
    for (const glm::vec2& ax : axes)
    {
        if (glm::dot(ax, ax) < 1e-12f)
        {
            continue; // 退化轴（三角形某条边长度为 0）：跳过
        }
        float rMinP = std::numeric_limits<float>::max();
        float rMaxP = -std::numeric_limits<float>::max();
        float tMinP = std::numeric_limits<float>::max();
        float tMaxP = -std::numeric_limits<float>::max();
        for (int i = 0; i < 4; ++i)
        {
            const float d = glm::dot(rect[i], ax);
            rMinP = std::min(rMinP, d);
            rMaxP = std::max(rMaxP, d);
        }
        for (int i = 0; i < 3; ++i)
        {
            const float d = glm::dot(tri[i], ax);
            tMinP = std::min(tMinP, d);
            tMaxP = std::max(tMaxP, d);
        }
        if (rMaxP < tMinP - 1e-6f || tMaxP < rMinP - 1e-6f)
        {
            return false;
        }
    }
    return true;
}

// Sutherland–Hodgman：用矩形裁剪凸/任意多边形（2D）。用于求三角形在某个格子内的精确 y 范围。
inline void ClipPolygonToRect(const std::vector<glm::vec2>& in, const glm::vec2& rMin, const glm::vec2& rMax,
                              std::vector<glm::vec2>& out)
{
    out = in;
    std::vector<glm::vec2> tmp;
    const glm::vec2 normals[4] = {glm::vec2(1.0f, 0.0f), glm::vec2(-1.0f, 0.0f), glm::vec2(0.0f, 1.0f),
                                  glm::vec2(0.0f, -1.0f)};
    const float offsets[4] = {rMin.x, -rMax.x, rMin.y, -rMax.y};
    for (int i = 0; i < 4; ++i)
    {
        if (out.empty())
        {
            return;
        }
        tmp.clear();
        const glm::vec2& n = normals[i];
        const float o = offsets[i];
        for (size_t j = 0; j < out.size(); ++j)
        {
            const glm::vec2& cur = out[j];
            const glm::vec2& nxt = out[(j + 1) % out.size()];
            const float dc = glm::dot(n, cur) - o;
            const float dn = glm::dot(n, nxt) - o;
            if (dc >= 0.0f)
            {
                tmp.push_back(cur);
            }
            if ((dc >= 0.0f) != (dn >= 0.0f))
            {
                const float t = dc / (dc - dn);
                tmp.push_back(cur + (nxt - cur) * t);
            }
        }
        out = tmp;
    }
}
} // namespace NavDetail

// 烘焙产物 + 查询入口。
class NavMesh
{
  public:
    // 一个凸多边形（三角形）。n[i] 是边 (v[i], v[(i+1)%3]) 的对侧多边形索引，-1 表示边界。
    struct Poly
    {
        int v[3] = {0, 0, 0};
        int n[3] = {-1, -1, -1};
    };

    struct Stats
    {
        int spans = 0;         // 体素化得到的段总数
        int walkableSpans = 0; // 通过坡度 + 净空过滤的段数
        int erodedSpans = 0;   // 通过 agent 半径膨胀后存活的段数
        int regions = 0;       // 连通域数量（经最小尺寸过滤后）
        int contourLoops = 0;  // 轮廓环数量（含孔洞环）
        int contourVertices = 0;
        int triangles = 0;
    };

    bool Build(const glm::vec3* verts, size_t vertexCount, const uint32_t* indices, size_t indexCount,
               const NavBuildSettings& settings)
    {
        Clear();
        if (verts == nullptr || indices == nullptr || vertexCount == 0 || indexCount < 3)
        {
            return false;
        }
        settings_ = settings;
        const float cs = std::max(1e-4f, settings.cellSize);
        const float ch = std::max(1e-4f, settings.cellHeight);

        glm::vec3 bmin(std::numeric_limits<float>::max());
        glm::vec3 bmax(-std::numeric_limits<float>::max());
        for (size_t i = 0; i < vertexCount; ++i)
        {
            bmin = glm::min(bmin, verts[i]);
            bmax = glm::max(bmax, verts[i]);
        }
        if (!(bmax.x > bmin.x) && !(bmax.z > bmin.z))
        {
            return false; // 完全退化：XZ 上是一个点
        }
        // XZ 留 2 格边距（保证膨胀有空间），Y 顶部留出站立净空的余量。
        const float pad = cs * 2.0f;
        bmin.x -= pad;
        bmin.z -= pad;
        bmax.x += pad;
        bmax.z += pad;
        bmax.y += ch * static_cast<float>(std::max(1, settings.walkableHeight) + 4);
        bmin_ = bmin;
        bmax_ = bmax;

        nx_ = std::max(1, static_cast<int>(std::ceil((bmax.x - bmin.x) / cs)));
        nz_ = std::max(1, static_cast<int>(std::ceil((bmax.z - bmin.z) / cs)));
        nh_ = std::max(1, static_cast<int>(std::ceil((bmax.y - bmin.y) / ch)));
        cellSize_ = cs;
        cellHeight_ = ch;
        columns_.assign(static_cast<size_t>(nx_) * nz_, {});

        RasterizeTriangles(verts, indices, indexCount, cs, ch);
        MarkWalkableSpans();
        BuildRegionsAndErode();
        BuildContoursAndTriangulate();
        return true;
    }

    void Clear()
    {
        columns_.clear();
        vertices_.clear();
        polys_.clear();
        boundsMin_ = glm::vec3(0.0f);
        boundsMax_ = glm::vec3(0.0f);
        bmin_ = glm::vec3(0.0f);
        bmax_ = glm::vec3(0.0f);
        nx_ = nz_ = nh_ = 0;
        stats_ = Stats{};
        settings_ = NavBuildSettings{};
        cellSize_ = cellHeight_ = 0.25f;
    }

    bool IsEmpty() const { return polys_.empty(); }
    int PolyCount() const { return static_cast<int>(polys_.size()); }
    int VertexCount() const { return static_cast<int>(vertices_.size()); }
    const glm::vec3& Vertex(int i) const { return vertices_[static_cast<size_t>(i)]; }
    const Poly& PolyAt(int i) const { return polys_[static_cast<size_t>(i)]; }
    const Stats& GetStats() const { return stats_; }
    const NavBuildSettings& Settings() const { return settings_; }
    glm::vec3 BoundsMin() const { return boundsMin_; }
    glm::vec3 BoundsMax() const { return boundsMax_; }

    glm::vec3 PolyCenter(int i) const
    {
        const Poly& p = polys_[static_cast<size_t>(i)];
        return (vertices_[p.v[0]] + vertices_[p.v[1]] + vertices_[p.v[2]]) / 3.0f;
    }

    // 可行走总面积（XZ 投影，平方米）。
    float TotalArea() const
    {
        float area = 0.0f;
        for (const Poly& p : polys_)
        {
            const glm::vec2 a = NavDetail::XZ(vertices_[p.v[0]]);
            const glm::vec2 b = NavDetail::XZ(vertices_[p.v[1]]);
            const glm::vec2 c = NavDetail::XZ(vertices_[p.v[2]]);
            area += std::fabs(NavDetail::Cross2(a, b, c)) * 0.5f;
        }
        return area;
    }

  private:
    struct Span
    {
        int y0 = 0;           // 实心体素区间下界（含）
        int y1 = 0;           // 实心体素区间上界（含）；顶面高度 = (y1+1) 号体素的底部
        float topY = 0.0f;    // 该段顶面的几何高度（世界坐标），用于生成导航面
        bool slopeOk = false; // 是否有坡度达标的三角面贡献到本段
        bool walkable = false;
        int dist = 0;   // 到区域边界的切比雪夫距离（体素数）
        int region = 0; // 连通域 id，0 = 未分类
    };

    struct SpanRef
    {
        size_t cell = 0;
        size_t index = 0;
    };

    struct ContourVert
    {
        float x = 0.0f; // 网格单位（整数格点）
        float z = 0.0f;
        float y = 0.0f; // 世界高度
    };

    struct Loop
    {
        std::vector<ContourVert> verts;
        bool isHole = false;
    };

    void RasterizeTriangles(const glm::vec3* verts, const uint32_t* indices, size_t indexCount, float cs, float ch)
    {
        const float invCs = 1.0f / cs;
        // 坡度按「三角面法线与竖直方向夹角」判定（与 Unity / Recast 一致）：
        // 连续陡坡不会被 walkableClimb 漏判，climb 只负责台阶这一件事。
        const float cosSlope = std::cos(glm::radians(std::max(1.0f, std::min(89.0f, settings_.walkableSlopeDegrees))));
        const float cosSlope2 = cosSlope * cosSlope;
        for (size_t t = 0; t + 2 < indexCount; t += 3)
        {
            const glm::vec3& a = verts[indices[t]];
            const glm::vec3& b = verts[indices[t + 1]];
            const glm::vec3& c = verts[indices[t + 2]];
            const glm::vec3 e1 = b - a;
            const glm::vec3 e2 = c - a;
            const glm::vec3 nrm = glm::cross(e1, e2);
            const bool vertical = std::fabs(nrm.y) < 1e-9f;
            const float len2 = glm::dot(nrm, nrm);
            const bool slopeOk = (len2 > 1e-12f) && (nrm.y * nrm.y >= cosSlope2 * len2);

            const float minX = std::min(a.x, std::min(b.x, c.x));
            const float maxX = std::max(a.x, std::max(b.x, c.x));
            const float minZ = std::min(a.z, std::min(b.z, c.z));
            const float maxZ = std::max(a.z, std::max(b.z, c.z));
            const float minY = std::min(a.y, std::min(b.y, c.y));
            const float maxY = std::max(a.y, std::max(b.y, c.y));

            int x0 = static_cast<int>(std::floor((minX - bmin_.x) * invCs));
            int x1 = static_cast<int>(std::floor((maxX - bmin_.x) * invCs));
            int z0 = static_cast<int>(std::floor((minZ - bmin_.z) * invCs));
            int z1 = static_cast<int>(std::floor((maxZ - bmin_.z) * invCs));
            x0 = std::max(0, std::min(nx_ - 1, x0));
            x1 = std::max(0, std::min(nx_ - 1, x1));
            z0 = std::max(0, std::min(nz_ - 1, z0));
            z1 = std::max(0, std::min(nz_ - 1, z1));

            const glm::vec2 t0 = NavDetail::XZ(a);
            const glm::vec2 t1b = NavDetail::XZ(b);
            const glm::vec2 t2 = NavDetail::XZ(c);
            const glm::vec3 nn = nrm;

            for (int z = z0; z <= z1; ++z)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    const glm::vec2 rMin(bmin_.x + x * cs, bmin_.z + z * cs);
                    const glm::vec2 rMax = rMin + glm::vec2(cs, cs);
                    if (!NavDetail::RectOverlapsTriangle2D(rMin, rMax, t0, t1b, t2))
                    {
                        continue;
                    }
                    float yLo = 0.0f;
                    float yHi = 0.0f;
                    if (vertical)
                    {
                        yLo = minY;
                        yHi = maxY;
                    }
                    else
                    {
                        std::vector<glm::vec2> clipped;
                        NavDetail::ClipPolygonToRect({t0, t1b, t2}, rMin, rMax, clipped);
                        if (clipped.empty())
                        {
                            continue;
                        }
                        // 平面方程 n·(p - a) = 0 → y = (n·a - n.x*x - n.z*z) / n.y
                        const float d = glm::dot(nn, a);
                        yLo = std::numeric_limits<float>::max();
                        yHi = -std::numeric_limits<float>::max();
                        for (const glm::vec2& p : clipped)
                        {
                            const float y = (d - nn.x * p.x - nn.z * p.y) / nn.y;
                            yLo = std::min(yLo, y);
                            yHi = std::max(yHi, y);
                        }
                    }
                    const int vy0 = std::max(0, std::min(nh_ - 1, static_cast<int>(std::floor((yLo - bmin_.y) / ch))));
                    const int vy1 = std::max(0, std::min(nh_ - 1, static_cast<int>(std::floor((yHi - bmin_.y) / ch))));
                    AddSpan(static_cast<size_t>(z) * nx_ + x, vy0, vy1, yHi, slopeOk);
                }
            }
        }
        int total = 0;
        for (auto& col : columns_)
        {
            total += static_cast<int>(col.size());
        }
        stats_.spans = total;
    }

    void AddSpan(size_t colIdx, int y0, int y1, float topY, bool slopeOk)
    {
        std::vector<Span>& col = columns_[colIdx];
        for (Span& s : col)
        {
            // 重叠或紧邻（y1+1 == y0）即合并，保持「实心连续体」语义。
            if (y0 <= s.y1 + 1 && s.y0 <= y1 + 1)
            {
                s.y0 = std::min(s.y0, y0);
                s.y1 = std::max(s.y1, y1);
                s.topY = std::max(s.topY, topY);
                s.slopeOk = s.slopeOk || slopeOk;
                return;
            }
        }
        Span s;
        s.y0 = y0;
        s.y1 = y1;
        s.topY = topY;
        s.slopeOk = slopeOk;
        col.push_back(s);
        std::sort(col.begin(), col.end(), [](const Span& a, const Span& b) { return a.y0 < b.y0; });
    }

    void MarkWalkableSpans()
    {
        const int clearance = std::max(1, settings_.walkableHeight);
        int walkable = 0;
        for (size_t ci = 0; ci < columns_.size(); ++ci)
        {
            std::vector<Span>& col = columns_[ci];
            for (size_t si = 0; si < col.size(); ++si)
            {
                Span& s = col[si];
                // (1) 坡度：由贡献到本段的三角面法线决定（见 RasterizeTriangles）。
                if (!s.slopeOk)
                {
                    continue;
                }
                // (2) 站立净空：顶面上方必须留出 clearance 个空体素。
                int free = nh_ - 1 - s.y1;
                if (si + 1 < col.size())
                {
                    free = col[si + 1].y0 - (s.y1 + 1);
                }
                if (free < clearance)
                {
                    continue;
                }
                s.walkable = true;
                ++walkable;
            }
        }
        stats_.walkableSpans = walkable;
    }

    static bool SpansConnect(const Span& a, const Span& b, int climb)
    {
        return a.walkable && b.walkable && std::abs(a.y1 - b.y1) <= climb;
    }

    void BuildRegionsAndErode()
    {
        const int climb = std::max(0, settings_.walkableClimb);
        const int radius = std::max(0, settings_.walkableRadius);
        const size_t cellCount = static_cast<size_t>(nx_) * nz_;

        // 泛洪：同时算出「到边界的距离」与「连通域 id」。
        int regionId = 0;
        for (size_t ci = 0; ci < cellCount; ++ci)
        {
            for (size_t si = 0; si < columns_[ci].size(); ++si)
            {
                Span& s = columns_[ci][si];
                if (!s.walkable || s.region != 0)
                {
                    continue;
                }
                ++regionId;
                s.region = regionId;
                s.dist = std::numeric_limits<int>::max() / 4;
                std::vector<SpanRef> wave;
                wave.push_back({ci, si});
                std::vector<SpanRef> region;
                region.push_back({ci, si});
                // 第一遍：连通域泛洪（BFS）
                for (size_t head = 0; head < wave.size(); ++head)
                {
                    const size_t cIdx = wave[head].cell;
                    const size_t sIdx = wave[head].index;
                    const int gx = static_cast<int>(cIdx % static_cast<size_t>(nx_));
                    const int gz = static_cast<int>(cIdx / static_cast<size_t>(nx_));
                    for (int oz = -1; oz <= 1; ++oz)
                    {
                        for (int ox = -1; ox <= 1; ++ox)
                        {
                            if (ox == 0 && oz == 0)
                            {
                                continue;
                            }
                            const int nxp = gx + ox;
                            const int nzp = gz + oz;
                            if (nxp < 0 || nxp >= nx_ || nzp < 0 || nzp >= nz_)
                            {
                                continue;
                            }
                            const size_t nci = static_cast<size_t>(nzp) * nx_ + nxp;
                            for (size_t k = 0; k < columns_[nci].size(); ++k)
                            {
                                Span& t = columns_[nci][k];
                                if (t.region != 0 || !SpansConnect(columns_[cIdx][sIdx], t, climb))
                                {
                                    continue;
                                }
                                t.region = regionId;
                                t.dist = std::numeric_limits<int>::max() / 4;
                                wave.emplace_back(nci, k);
                                region.push_back({nci, k});
                            }
                        }
                    }
                }
                // 第二遍：多源 BFS 求到边界的切比雪夫距离。
                std::vector<SpanRef> sources;
                for (const SpanRef& r : region)
                {
                    if (IsBoundarySpan(r.cell, r.index, climb))
                    {
                        columns_[r.cell][r.index].dist = 0;
                        sources.push_back(r);
                    }
                }
                for (size_t head = 0; head < sources.size(); ++head)
                {
                    const SpanRef cur = sources[head];
                    const int gx = static_cast<int>(cur.cell % static_cast<size_t>(nx_));
                    const int gz = static_cast<int>(cur.cell / static_cast<size_t>(nx_));
                    for (int oz = -1; oz <= 1; ++oz)
                    {
                        for (int ox = -1; ox <= 1; ++ox)
                        {
                            if (ox == 0 && oz == 0)
                            {
                                continue;
                            }
                            const int nxp = gx + ox;
                            const int nzp = gz + oz;
                            if (nxp < 0 || nxp >= nx_ || nzp < 0 || nzp >= nz_)
                            {
                                continue;
                            }
                            const size_t nci = static_cast<size_t>(nzp) * nx_ + nxp;
                            for (size_t k = 0; k < columns_[nci].size(); ++k)
                            {
                                Span& t = columns_[nci][k];
                                if (t.region != regionId)
                                {
                                    continue;
                                }
                                if (!SpansConnect(columns_[cur.cell][cur.index], t, climb))
                                {
                                    continue;
                                }
                                const int nd = columns_[cur.cell][cur.index].dist + 1;
                                if (nd < t.dist)
                                {
                                    t.dist = nd;
                                    sources.push_back({nci, k});
                                }
                            }
                        }
                    }
                }
            }
        }

        // 按最小尺寸过滤连通域，并按膨胀半径剔除边缘段。
        std::unordered_map<int, int> regionSize;
        for (size_t ci = 0; ci < cellCount; ++ci)
        {
            for (const Span& s : columns_[ci])
            {
                if (s.walkable && s.region > 0)
                {
                    regionSize[s.region] += 1;
                }
            }
        }
        const int minSpans = std::max(1, settings_.minRegionSpans);
        int eroded = 0;
        std::unordered_map<int, char> kept;
        for (const auto& kv : regionSize)
        {
            if (kv.second >= minSpans)
            {
                kept[kv.first] = 1;
            }
        }
        for (size_t ci = 0; ci < cellCount; ++ci)
        {
            for (Span& s : columns_[ci])
            {
                if (s.walkable && s.region > 0 && kept.count(s.region) != 0 && s.dist >= radius)
                {
                    ++eroded;
                }
                else
                {
                    s.walkable = false;
                }
            }
        }
        stats_.erodedSpans = eroded;
        stats_.regions = static_cast<int>(kept.size());
    }

    bool IsBoundarySpan(size_t cellIdx, size_t spanIdx, int climb) const
    {
        const int gx = static_cast<int>(cellIdx % static_cast<size_t>(nx_));
        const int gz = static_cast<int>(cellIdx / static_cast<size_t>(nx_));
        const Span& s = columns_[cellIdx][spanIdx];
        for (int oz = -1; oz <= 1; ++oz)
        {
            for (int ox = -1; ox <= 1; ++ox)
            {
                if (ox == 0 && oz == 0)
                {
                    continue;
                }
                const int nxp = gx + ox;
                const int nzp = gz + oz;
                if (nxp < 0 || nxp >= nx_ || nzp < 0 || nzp >= nz_)
                {
                    return true;
                }
                const size_t nci = static_cast<size_t>(nzp) * nx_ + nxp;
                bool ok = false;
                for (const Span& t : columns_[nci])
                {
                    if (SpansConnect(s, t, climb))
                    {
                        ok = true;
                        break;
                    }
                }
                if (!ok)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // ---- 轮廓 → 三角化 ----
    void BuildContoursAndTriangulate()
    {
        const size_t cellCount = static_cast<size_t>(nx_) * nz_;
        std::unordered_map<int, std::vector<char>> masks;
        std::unordered_map<int, std::vector<float>> heights;
        std::unordered_map<int, int> sizes;
        for (size_t ci = 0; ci < cellCount; ++ci)
        {
            for (const Span& s : columns_[ci])
            {
                if (!s.walkable || s.region <= 0)
                {
                    continue;
                }
                auto& m = masks[s.region];
                auto& h = heights[s.region];
                if (m.empty())
                {
                    m.assign(cellCount, 0);
                    h.assign(cellCount, 0.0f);
                }
                if (m[ci] == 0 || s.topY > h[ci])
                {
                    m[ci] = 1;
                    h[ci] = s.topY;
                }
                sizes[s.region] += 1;
            }
        }

        std::vector<int> regionIds;
        for (const auto& kv : sizes)
        {
            regionIds.push_back(kv.first);
        }
        std::sort(regionIds.begin(), regionIds.end());

        std::vector<glm::vec3> allVerts;
        std::vector<Poly> allPolys;

        for (int rid : regionIds)
        {
            const std::vector<char>& mask = masks[rid];
            const std::vector<float>& hgt = heights[rid];
            std::vector<Loop> loops = TraceLoops(mask, hgt);
            for (Loop& l : loops)
            {
                SimplifyLoop(l);
            }
            stats_.contourLoops += static_cast<int>(loops.size());

            std::vector<Loop> merged = MergeHoles(loops);
            for (const Loop& l : merged)
            {
                stats_.contourVertices += static_cast<int>(l.verts.size());
                std::vector<glm::vec2> poly2;
                std::vector<float> ys;
                poly2.reserve(l.verts.size());
                ys.reserve(l.verts.size());
                for (const ContourVert& cv : l.verts)
                {
                    poly2.emplace_back(bmin_.x + cv.x * cellSize_, bmin_.z + cv.z * cellSize_);
                    ys.push_back(cv.y);
                }
                // 保证 CCW
                if (NavDetail::SignedArea2(poly2) < 0.0f)
                {
                    std::reverse(poly2.begin(), poly2.end());
                    std::reverse(ys.begin(), ys.end());
                }
                std::vector<int> tris;
                const bool earOk = EarClip(poly2, tris);
                if (tris.size() < 3)
                {
                    continue;
                }
                const int base = static_cast<int>(allVerts.size());
                for (size_t i = 0; i < poly2.size(); ++i)
                {
                    allVerts.emplace_back(poly2[i].x, ys[i], poly2[i].y);
                }
                for (size_t t = 0; t + 2 < tris.size(); t += 3)
                {
                    const int i0 = base + tris[t];
                    const int i1 = base + tris[t + 1];
                    const int i2 = base + tris[t + 2];
                    const glm::vec2 a(poly2[tris[t]].x, poly2[tris[t]].y);
                    const glm::vec2 b(poly2[tris[t + 1]].x, poly2[tris[t + 1]].y);
                    const glm::vec2 c(poly2[tris[t + 2]].x, poly2[tris[t + 2]].y);
                    if (std::fabs(NavDetail::Cross2(a, b, c)) * 0.5f < 1e-7f)
                    {
                        continue; // 退化（搭桥产生的零面积三角形）
                    }
                    // 统一 CCW（XZ 平面叉积 > 0）
                    if (NavDetail::Cross2(a, b, c) < 0.0f)
                    {
                        allPolys.push_back({{i0, i2, i1}, {-1, -1, -1}});
                    }
                    else
                    {
                        allPolys.push_back({{i0, i1, i2}, {-1, -1, -1}});
                    }
                }
            }
        }

        // 顶点焊接：相同 XZ + 相近 Y 的顶点合并，保证跨多边形的邻接能被识别。
        WeldVertices(allVerts, allPolys);
        vertices_ = std::move(allVerts);
        polys_ = std::move(allPolys);
        BuildAdjacency();
        stats_.triangles = static_cast<int>(polys_.size());
        if (!vertices_.empty())
        {
            boundsMin_ = vertices_[0];
            boundsMax_ = vertices_[0];
            for (const glm::vec3& v : vertices_)
            {
                boundsMin_ = glm::min(boundsMin_, v);
                boundsMax_ = glm::max(boundsMax_, v);
            }
        }
    }

    // 有向边串联：每个区域格子朝「缺失邻居」的一侧发射一条 CCW 边，再按端点串环。
    std::vector<Loop> TraceLoops(const std::vector<char>& mask, const std::vector<float>& hgt) const
    {
        struct Edge
        {
            int sx, sz, ex, ez;
            float y;
            bool used = false;
        };
        std::vector<Edge> edges;
        auto InRegion = [&](int x, int z)
        {
            if (x < 0 || x >= nx_ || z < 0 || z >= nz_)
            {
                return false;
            }
            return mask[static_cast<size_t>(z) * nx_ + x] != 0;
        };
        auto CellY = [&](int x, int z) { return hgt[static_cast<size_t>(z) * nx_ + x]; };
        for (int z = 0; z < nz_; ++z)
        {
            for (int x = 0; x < nx_; ++x)
            {
                if (!InRegion(x, z))
                {
                    continue;
                }
                const float y = CellY(x, z);
                if (!InRegion(x, z - 1))
                {
                    edges.push_back({x, z, x + 1, z, y, false});
                }
                if (!InRegion(x + 1, z))
                {
                    edges.push_back({x + 1, z, x + 1, z + 1, y, false});
                }
                if (!InRegion(x, z + 1))
                {
                    edges.push_back({x + 1, z + 1, x, z + 1, y, false});
                }
                if (!InRegion(x - 1, z))
                {
                    edges.push_back({x, z + 1, x, z, y, false});
                }
            }
        }
        std::unordered_map<long long, std::vector<size_t>> byStart;
        for (size_t i = 0; i < edges.size(); ++i)
        {
            const long long key = (static_cast<long long>(edges[i].sx) << 32) | static_cast<uint32_t>(edges[i].sz);
            byStart[key].push_back(i);
        }
        std::vector<Loop> loops;
        for (size_t seed = 0; seed < edges.size(); ++seed)
        {
            if (edges[seed].used)
            {
                continue;
            }
            Loop loop;
            size_t cur = seed;
            const int startX = edges[seed].sx;
            const int startZ = edges[seed].sz;
            bool closed = false;
            for (size_t guard = 0; guard <= edges.size(); ++guard)
            {
                if (edges[cur].used)
                {
                    break;
                }
                edges[cur].used = true;
                loop.verts.push_back(
                    {static_cast<float>(edges[cur].sx), static_cast<float>(edges[cur].sz), edges[cur].y});
                if (edges[cur].ex == startX && edges[cur].ez == startZ)
                {
                    closed = true;
                    break;
                }
                const long long key =
                    (static_cast<long long>(edges[cur].ex) << 32) | static_cast<uint32_t>(edges[cur].ez);
                auto it = byStart.find(key);
                if (it == byStart.end())
                {
                    break;
                }
                size_t next = std::numeric_limits<size_t>::max();
                for (size_t cand : it->second)
                {
                    if (!edges[cand].used)
                    {
                        next = cand;
                        break;
                    }
                }
                if (next == std::numeric_limits<size_t>::max())
                {
                    break;
                }
                cur = next;
            }
            if (!closed || loop.verts.size() < 3)
            {
                continue;
            }
            std::vector<glm::vec2> p2;
            for (const ContourVert& cv : loop.verts)
            {
                p2.emplace_back(cv.x, cv.z);
            }
            loop.isHole = NavDetail::SignedArea2(p2) < 0.0f;
            loops.push_back(std::move(loop));
        }
        return loops;
    }

    // Douglas–Peucker：闭合环上取相距最远的两点做锚点，拆成两条开链分别简化。
    void SimplifyLoop(Loop& loop) const
    {
        const float tol = std::max(0.0f, settings_.maxSimplificationError);
        const size_t n = loop.verts.size();
        if (n < 4)
        {
            return;
        }
        size_t a = 0;
        size_t b = 0;
        float bestD = -1.0f;
        for (size_t i = 0; i < n; ++i)
        {
            for (size_t j = i + 1; j < n; ++j)
            {
                const float dx = loop.verts[i].x - loop.verts[j].x;
                const float dz = loop.verts[i].z - loop.verts[j].z;
                const float d = dx * dx + dz * dz;
                if (d > bestD)
                {
                    bestD = d;
                    a = i;
                    b = j;
                }
            }
        }
        std::vector<ContourVert> chain1;
        std::vector<ContourVert> chain2;
        for (size_t i = a;; i = (i + 1) % n)
        {
            chain1.push_back(loop.verts[i]);
            if (i == b)
            {
                break;
            }
        }
        for (size_t i = b;; i = (i + 1) % n)
        {
            chain2.push_back(loop.verts[i]);
            if (i == a)
            {
                break;
            }
        }
        std::vector<ContourVert> s1;
        std::vector<ContourVert> s2;
        DouglasPeucker(chain1, tol, s1);
        DouglasPeucker(chain2, tol, s2);
        std::vector<ContourVert> out = s1;
        for (size_t i = 1; i + 1 < s2.size(); ++i)
        {
            out.push_back(s2[i]);
        }
        if (settings_.maxEdgeLength > 0)
        {
            SubdivideLoop(out, static_cast<float>(settings_.maxEdgeLength));
        }
        loop.verts = std::move(out);
    }

    static void DouglasPeucker(const std::vector<ContourVert>& in, float tol, std::vector<ContourVert>& out)
    {
        out.clear();
        if (in.empty())
        {
            return;
        }
        std::vector<char> keep(in.size(), 0);
        keep.front() = 1;
        keep.back() = 1;
        std::vector<std::pair<size_t, size_t>> stack;
        stack.emplace_back(0, in.size() - 1);
        while (!stack.empty())
        {
            const auto range = stack.back();
            stack.pop_back();
            float maxD = -1.0f;
            size_t maxI = range.first;
            const glm::vec2 a(in[range.first].x, in[range.first].z);
            const glm::vec2 b(in[range.second].x, in[range.second].z);
            const glm::vec2 ab = b - a;
            const float len = std::max(1e-9f, glm::length(ab));
            for (size_t i = range.first + 1; i < range.second; ++i)
            {
                const glm::vec2 p(in[i].x, in[i].z);
                const float d = std::fabs(NavDetail::Cross2(a, b, p)) / len;
                if (d > maxD)
                {
                    maxD = d;
                    maxI = i;
                }
            }
            if (maxD > tol && maxI > range.first && maxI < range.second)
            {
                keep[maxI] = 1;
                stack.emplace_back(range.first, maxI);
                stack.emplace_back(maxI, range.second);
            }
        }
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (keep[i] != 0)
            {
                out.push_back(in[i]);
            }
        }
    }

    static void SubdivideLoop(std::vector<ContourVert>& loop, float maxLen)
    {
        std::vector<ContourVert> out;
        const size_t n = loop.size();
        for (size_t i = 0; i < n; ++i)
        {
            const ContourVert& a = loop[i];
            const ContourVert& b = loop[(i + 1) % n];
            out.push_back(a);
            const float dx = b.x - a.x;
            const float dz = b.z - a.z;
            const float len = std::sqrt(dx * dx + dz * dz);
            const int seg = static_cast<int>(std::ceil(len / maxLen));
            for (int k = 1; k < seg; ++k)
            {
                const float t = static_cast<float>(k) / static_cast<float>(seg);
                out.push_back({a.x + dx * t, a.z + dz * t, a.y + (b.y - a.y) * t});
            }
        }
        loop = std::move(out);
    }

    // 把孔洞环「搭桥」并入包含它的外环，得到可耳切的单个多边形。
    static std::vector<Loop> MergeHoles(std::vector<Loop>& loops)
    {
        std::vector<Loop> outers;
        std::vector<Loop> holes;
        for (Loop& l : loops)
        {
            if (l.isHole)
            {
                holes.push_back(std::move(l));
            }
            else
            {
                outers.push_back(std::move(l));
            }
        }
        std::vector<Loop> result = std::move(outers);
        for (Loop& h : holes)
        {
            int bestOuter = -1;
            size_t bestI = 0;
            size_t bestJ = 0;
            float bestD = std::numeric_limits<float>::max();
            for (size_t oi = 0; oi < result.size(); ++oi)
            {
                std::vector<glm::vec2> op;
                for (const ContourVert& cv : result[oi].verts)
                {
                    op.emplace_back(cv.x, cv.z);
                }
                // 用孔洞任一点做包含测试（先取第一个点判断所属外环）
                bool inside = false;
                for (int attempt = 0; attempt < 2 && !inside; ++attempt)
                {
                    const glm::vec2 q(h.verts[attempt % h.verts.size()].x, h.verts[attempt % h.verts.size()].z);
                    bool odd = false;
                    for (size_t i = 0, j = op.size() - 1; i < op.size(); j = i++)
                    {
                        if (((op[i].y > q.y) != (op[j].y > q.y)) &&
                            (q.x < (op[j].x - op[i].x) * (q.y - op[i].y) / (op[j].y - op[i].y) + op[i].x))
                        {
                            odd = !odd;
                        }
                    }
                    inside = odd;
                }
                if (!inside)
                {
                    continue;
                }
                for (size_t i = 0; i < result[oi].verts.size(); ++i)
                {
                    for (size_t j = 0; j < h.verts.size(); ++j)
                    {
                        const float dx = result[oi].verts[i].x - h.verts[j].x;
                        const float dz = result[oi].verts[i].z - h.verts[j].z;
                        const float d = dx * dx + dz * dz;
                        if (d < bestD)
                        {
                            bestD = d;
                            bestOuter = static_cast<int>(oi);
                            bestI = i;
                            bestJ = j;
                        }
                    }
                }
                break;
            }
            if (bestOuter < 0)
            {
                continue; // 找不到宿主（理论上不该发生）：丢弃该孔洞
            }
            Loop& o = result[static_cast<size_t>(bestOuter)];
            const size_t nO = o.verts.size();
            const size_t nH = h.verts.size();
            std::vector<ContourVert> merged;
            merged.reserve(nO + nH + 2);
            for (size_t k = 0; k <= bestI; ++k)
            {
                merged.push_back(o.verts[k]);
            }
            for (size_t k = 0; k < nH; ++k)
            {
                merged.push_back(h.verts[(bestJ + k) % nH]);
            }
            merged.push_back(h.verts[bestJ]);
            merged.push_back(o.verts[bestI]);
            for (size_t k = 1; k < nO; ++k)
            {
                merged.push_back(o.verts[(bestI + k) % nO]);
            }
            o.verts = std::move(merged);
        }
        return result;
    }

    // 耳切法：把简单多边形（CCW）三角化。搭桥产生的退化三角形会在上层被剔除。
    static bool EarClip(const std::vector<glm::vec2>& poly, std::vector<int>& outTris)
    {
        outTris.clear();
        const size_t n = poly.size();
        if (n < 3)
        {
            return false;
        }
        std::vector<int> idx(n);
        std::iota(idx.begin(), idx.end(), 0);
        size_t count = n;
        size_t guard = 0;
        while (count > 3)
        {
            bool clipped = false;
            for (size_t k = 0; k < count; ++k)
            {
                const int i0 = idx[(k + count - 1) % count];
                const int i1 = idx[k];
                const int i2 = idx[(k + 1) % count];
                if (NavDetail::Cross2(poly[i0], poly[i1], poly[i2]) <= 1e-9f)
                {
                    continue; // 凹点或退化
                }
                bool contains = false;
                for (size_t m = 0; m < count; ++m)
                {
                    const int vi = idx[m];
                    if (vi == i0 || vi == i1 || vi == i2)
                    {
                        continue;
                    }
                    // 仅统计「严格内部」的顶点：落在耳三角形边上的点（孔洞搭桥产生的重合顶点、
                    // 共线点）不应阻止该耳被裁掉——否则搭桥带来的零宽度狭缝会把所有合法耳都误判掉。
                    const glm::vec2& pp = poly[vi];
                    const float e1 = NavDetail::Cross2(poly[i0], poly[i1], pp);
                    const float e2 = NavDetail::Cross2(poly[i1], poly[i2], pp);
                    const float e3 = NavDetail::Cross2(poly[i2], poly[i0], pp);
                    const bool strictIn = (e1 > 1e-9f && e2 > 1e-9f && e3 > 1e-9f) ||
                                          (e1 < -1e-9f && e2 < -1e-9f && e3 < -1e-9f);
                    if (strictIn)
                    {
                        contains = true;
                        break;
                    }
                }
                if (contains)
                {
                    continue;
                }
                outTris.push_back(i0);
                outTris.push_back(i1);
                outTris.push_back(i2);
                idx.erase(idx.begin() + static_cast<long>(k));
                --count;
                clipped = true;
                break;
            }
            if (!clipped)
            {
                return false;
            }
            if (++guard > n * n + 16)
            {
                return false;
            }
        }
        outTris.push_back(idx[0]);
        outTris.push_back(idx[1]);
        outTris.push_back(idx[2]);
        return true;
    }

    static void WeldVertices(std::vector<glm::vec3>& verts, std::vector<Poly>& polys)
    {
        struct Key
        {
            long long x, y, z;
            bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
        };
        struct Hash
        {
            size_t operator()(const Key& k) const
            {
                return static_cast<size_t>(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL);
            }
        };
        std::unordered_map<Key, int, Hash> map;
        std::vector<int> remap(verts.size(), -1);
        std::vector<glm::vec3> out;
        for (size_t i = 0; i < verts.size(); ++i)
        {
            Key k;
            k.x = static_cast<long long>(std::llround(verts[i].x * 1000.0f));
            k.y = static_cast<long long>(std::llround(verts[i].y * 1000.0f));
            k.z = static_cast<long long>(std::llround(verts[i].z * 1000.0f));
            auto it = map.find(k);
            if (it != map.end())
            {
                remap[i] = it->second;
            }
            else
            {
                const int ni = static_cast<int>(out.size());
                map.emplace(k, ni);
                remap[i] = ni;
                out.push_back(verts[i]);
            }
        }
        for (Poly& p : polys)
        {
            for (int i = 0; i < 3; ++i)
            {
                p.v[i] = remap[static_cast<size_t>(p.v[i])];
            }
        }
        verts = std::move(out);
    }

    void BuildAdjacency()
    {
        struct EdgeKey
        {
            int a, b;
            bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
        };
        struct EdgeHash
        {
            size_t operator()(const EdgeKey& k) const { return static_cast<size_t>(k.a * 4093 + k.b); }
        };
        std::unordered_map<EdgeKey, std::pair<int, int>, EdgeHash> owners;
        for (size_t pi = 0; pi < polys_.size(); ++pi)
        {
            for (int e = 0; e < 3; ++e)
            {
                const int v0 = polys_[pi].v[e];
                const int v1 = polys_[pi].v[(e + 1) % 3];
                const EdgeKey key{std::min(v0, v1), std::max(v0, v1)};
                auto it = owners.find(key);
                if (it == owners.end())
                {
                    owners.emplace(key, std::make_pair(static_cast<int>(pi), e));
                }
                else
                {
                    const int other = it->second.first;
                    const int otherEdge = it->second.second;
                    polys_[pi].n[e] = other;
                    polys_[static_cast<size_t>(other)].n[otherEdge] = static_cast<int>(pi);
                }
            }
        }
    }

    std::vector<std::vector<Span>> columns_;
    std::vector<glm::vec3> vertices_;
    std::vector<Poly> polys_;
    glm::vec3 boundsMin_{0.0f};
    glm::vec3 boundsMax_{0.0f};
    glm::vec3 bmin_{0.0f};
    glm::vec3 bmax_{0.0f};
    int nx_ = 0;
    int nz_ = 0;
    int nh_ = 0;
    float cellSize_ = 0.25f;
    float cellHeight_ = 0.25f;
    Stats stats_{};
    NavBuildSettings settings_{};

    friend class NavMeshQuery;
};

// 只读查询器：最近点投影 + A*（多边形图）+ 漏斗拉直。可并发调用。
class NavMeshQuery
{
  public:
    struct NearestPointResult
    {
        bool found = false;
        int poly = -1;
        glm::vec3 point{0.0f};
        float distance = 0.0f; // 水平距离
    };

    struct Path
    {
        bool found = false;
        std::vector<glm::vec3> points; // 已拉直的路点（含起点与终点）
        std::vector<int> polys;        //  corridor：A* 走过的多边形序列
        float length = 0.0f;
    };

    explicit NavMeshQuery(const NavMesh& mesh) : mesh_(mesh) {}

    NearestPointResult FindNearestPoint(const glm::vec3& pos, const glm::vec3& extents) const
    {
        NearestPointResult best;
        const glm::vec2 p = NavDetail::XZ(pos);
        float bestD = std::numeric_limits<float>::max();
        for (int i = 0; i < mesh_.PolyCount(); ++i)
        {
            const NavMesh::Poly& poly = mesh_.PolyAt(i);
            const glm::vec2 a = NavDetail::XZ(mesh_.Vertex(poly.v[0]));
            const glm::vec2 b = NavDetail::XZ(mesh_.Vertex(poly.v[1]));
            const glm::vec2 c = NavDetail::XZ(mesh_.Vertex(poly.v[2]));
            const glm::vec2 q = NavDetail::ClosestPointOnTriangle2(p, a, b, c);
            const float d = glm::distance(p, q);
            if (d > extents.x && d > extents.z)
            {
                continue;
            }
            float w0 = 0.0f;
            float w1 = 0.0f;
            float w2 = 0.0f;
            NavDetail::Barycentric2(q, a, b, c, w0, w1, w2);
            const float y =
                w0 * mesh_.Vertex(poly.v[0]).y + w1 * mesh_.Vertex(poly.v[1]).y + w2 * mesh_.Vertex(poly.v[2]).y;
            if (std::fabs(y - pos.y) > std::max(extents.y, 1e-3f))
            {
                continue;
            }
            if (d < bestD)
            {
                bestD = d;
                best.found = true;
                best.poly = i;
                best.point = glm::vec3(q.x, y, q.y);
                best.distance = d;
            }
        }
        return best;
    }

    Path FindPath(const glm::vec3& start, const glm::vec3& end, const glm::vec3& extents) const
    {
        Path result;
        const NearestPointResult ns = FindNearestPoint(start, extents);
        const NearestPointResult ne = FindNearestPoint(end, extents);
        if (!ns.found || !ne.found)
        {
            return result;
        }
        std::vector<int> corridor = Search(ns.poly, ne.poly);
        if (corridor.empty())
        {
            return result;
        }
        result.polys = corridor;
        std::vector<glm::vec2> way = StringPull(ns.point, ne.point, corridor);
        result.points.push_back(ns.point);
        for (const glm::vec2& w : way)
        {
            result.points.push_back(InterpolateHeight(w, corridor));
        }
        result.points.push_back(ne.point);
        // 去掉与起终点重合的冗余点
        std::vector<glm::vec3> clean;
        for (const glm::vec3& p : result.points)
        {
            if (clean.empty() || glm::distance(clean.back(), p) > 1e-4f)
            {
                clean.push_back(p);
            }
        }
        result.points = std::move(clean);
        result.found = result.points.size() >= 2;
        for (size_t i = 1; i < result.points.size(); ++i)
        {
            result.length += glm::distance(result.points[i - 1], result.points[i]);
        }
        return result;
    }

  private:
    // 多边形图上的 A*：代价 = 相邻多边形中心的水平距离。
    std::vector<int> Search(int startPoly, int endPoly) const
    {
        if (startPoly < 0 || endPoly < 0 || startPoly >= mesh_.PolyCount() || endPoly >= mesh_.PolyCount())
        {
            return {};
        }
        if (startPoly == endPoly)
        {
            return {startPoly};
        }
        const int n = mesh_.PolyCount();
        std::vector<float> g(static_cast<size_t>(n), std::numeric_limits<float>::max());
        std::vector<int> cameFrom(static_cast<size_t>(n), -1);
        std::vector<char> closed(static_cast<size_t>(n), 0);
        std::vector<int> open;
        auto Heuristic = [&](int a, int b)
        {
            const glm::vec3 ca = mesh_.PolyCenter(a);
            const glm::vec3 cb = mesh_.PolyCenter(b);
            return glm::distance(NavDetail::XZ(ca), NavDetail::XZ(cb));
        };
        g[static_cast<size_t>(startPoly)] = 0.0f;
        open.push_back(startPoly);
        while (!open.empty())
        {
            size_t bestI = 0;
            float bestF = std::numeric_limits<float>::max();
            for (size_t i = 0; i < open.size(); ++i)
            {
                const int p = open[i];
                const float f = g[static_cast<size_t>(p)] + Heuristic(p, endPoly);
                if (f < bestF)
                {
                    bestF = f;
                    bestI = i;
                }
            }
            const int cur = open[bestI];
            open.erase(open.begin() + static_cast<long>(bestI));
            if (cur == endPoly)
            {
                break;
            }
            if (closed[static_cast<size_t>(cur)] != 0)
            {
                continue;
            }
            closed[static_cast<size_t>(cur)] = 1;
            for (int e = 0; e < 3; ++e)
            {
                const int nb = mesh_.PolyAt(cur).n[e];
                if (nb < 0 || closed[static_cast<size_t>(nb)] != 0)
                {
                    continue;
                }
                const glm::vec3 ca = mesh_.PolyCenter(cur);
                const glm::vec3 cb = mesh_.PolyCenter(nb);
                const float step = glm::distance(NavDetail::XZ(ca), NavDetail::XZ(cb));
                const float ng = g[static_cast<size_t>(cur)] + step;
                if (ng < g[static_cast<size_t>(nb)] - 1e-6f)
                {
                    g[static_cast<size_t>(nb)] = ng;
                    cameFrom[static_cast<size_t>(nb)] = cur;
                    open.push_back(nb);
                }
            }
        }
        if (cameFrom[static_cast<size_t>(endPoly)] < 0)
        {
            return {};
        }
        std::vector<int> path;
        for (int cur = endPoly; cur != -1; cur = cameFrom[static_cast<size_t>(cur)])
        {
            path.push_back(cur);
            if (cur == startPoly)
            {
                break;
            }
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    // 取得「从 from 走到 to」的通道（portal）左右端点（XZ 世界坐标）。
    // 约定：多边形绕序为 CCW，因此 from 中形如 (v[i] → v[i+1]) 的共享边，其内部在左、
    // 对侧（to）在右；行进方向即为「向右穿出」，故沿行进方向的左侧 = v[i+1]、右侧 = v[i]。
    bool GetPortal(int from, int to, glm::vec2& left, glm::vec2& right) const
    {
        const NavMesh::Poly& pf = mesh_.PolyAt(from);
        for (int e = 0; e < 3; ++e)
        {
            if (pf.n[e] != to)
            {
                continue;
            }
            const glm::vec3& v0 = mesh_.Vertex(pf.v[e]);
            const glm::vec3& v1 = mesh_.Vertex(pf.v[(e + 1) % 3]);
            right = NavDetail::XZ(v0);
            left = NavDetail::XZ(v1);
            return true;
        }
        return false;
    }

    // Simple Stupid Funnel：沿 portal 序列把走廊「拉直」成最少拐点。
    std::vector<glm::vec2> StringPull(const glm::vec3& start, const glm::vec3& end,
                                      const std::vector<int>& corridor) const
    {
        std::vector<glm::vec2> out;
        if (corridor.size() < 2)
        {
            return out;
        }
        const glm::vec2 apex = NavDetail::XZ(start);
        glm::vec2 portalApex = apex;
        glm::vec2 portalLeft = apex;
        glm::vec2 portalRight = apex;
        size_t apexIndex = 0;
        size_t leftIndex = 0;
        size_t rightIndex = 0;
        for (size_t pi = 0; pi < corridor.size(); ++pi)
        {
            const NavMesh::Poly& pp = mesh_.PolyAt(corridor[pi]);
            const glm::vec2 va = NavDetail::XZ(mesh_.Vertex(pp.v[0]));
            const glm::vec2 vb = NavDetail::XZ(mesh_.Vertex(pp.v[1]));
            const glm::vec2 vc = NavDetail::XZ(mesh_.Vertex(pp.v[2]));
        }
        {
            glm::vec2 l;
            glm::vec2 r;
            if (GetPortal(corridor[0], corridor[1], l, r))
            {
                portalLeft = l;
                portalRight = r;
            }
        }
        for (size_t i = 1; i < corridor.size(); ++i)
        {
            if (i + 1 >= corridor.size())
            {
                break;
            }
            glm::vec2 l;
            glm::vec2 r;
            if (!GetPortal(corridor[i], corridor[i + 1], l, r))
            {
                continue;
            }
            // 收紧右侧
            if (NavDetail::Cross2(portalApex, portalRight, r) >= 0.0f)
            {
                if (glm::distance(portalApex, portalRight) < 1e-9f ||
                    NavDetail::Cross2(portalApex, portalLeft, r) > 0.0f)
                {
                    out.push_back(portalLeft);
                    portalApex = portalLeft;
                    apexIndex = leftIndex;
                    portalLeft = portalApex;
                    portalRight = portalApex;
                    leftIndex = apexIndex;
                    rightIndex = apexIndex;
                    i = apexIndex;
                    continue;
                }
                portalRight = r;
                rightIndex = i;
            }
            // 收紧左侧
            if (NavDetail::Cross2(portalApex, l, portalLeft) <= 0.0f)
            {
                if (glm::distance(portalApex, portalLeft) < 1e-9f ||
                    NavDetail::Cross2(portalApex, l, portalRight) > 0.0f)
                {
                    out.push_back(portalRight);
                    portalApex = portalRight;
                    apexIndex = rightIndex;
                    portalLeft = portalApex;
                    portalRight = portalApex;
                    leftIndex = apexIndex;
                    rightIndex = apexIndex;
                    i = apexIndex;
                    continue;
                }
                portalLeft = l;
                leftIndex = i;
            }
        }
        // 末端：把终点也纳入漏斗判定
        const glm::vec2 e2 = NavDetail::XZ(end);
        if (NavDetail::Cross2(portalApex, portalRight, e2) <= 0.0f)
        {
            if (glm::distance(portalApex, portalRight) < 1e-9f || NavDetail::Cross2(portalApex, portalLeft, e2) <= 0.0f)
            {
                out.push_back(portalLeft);
            }
        }
        if (NavDetail::Cross2(portalApex, e2, portalLeft) <= 0.0f)
        {
            if (glm::distance(portalApex, portalLeft) < 1e-9f || NavDetail::Cross2(portalApex, e2, portalRight) > 0.0f)
            {
                out.push_back(portalRight);
            }
        }
        // 去掉与起/终点重合的拐点
        std::vector<glm::vec2> clean;
        for (const glm::vec2& p : out)
        {
            if (glm::distance(p, apex) < 1e-4f || glm::distance(p, e2) < 1e-4f)
            {
                continue;
            }
            if (!clean.empty() && glm::distance(clean.back(), p) < 1e-4f)
            {
                continue;
            }
            clean.push_back(p);
        }
        return clean;
    }

    // 用走廊里包含该点的多边形做重心插值，得到贴合地面的高度。
    glm::vec3 InterpolateHeight(const glm::vec2& p, const std::vector<int>& corridor) const
    {
        float bestY = 0.0f;
        bool found = false;
        for (int pi : corridor)
        {
            const NavMesh::Poly& poly = mesh_.PolyAt(pi);
            const glm::vec2 a = NavDetail::XZ(mesh_.Vertex(poly.v[0]));
            const glm::vec2 b = NavDetail::XZ(mesh_.Vertex(poly.v[1]));
            const glm::vec2 c = NavDetail::XZ(mesh_.Vertex(poly.v[2]));
            if (!NavDetail::PointInTriangle2(p, a, b, c))
            {
                continue;
            }
            float w0 = 0.0f;
            float w1 = 0.0f;
            float w2 = 0.0f;
            NavDetail::Barycentric2(p, a, b, c, w0, w1, w2);
            bestY = w0 * mesh_.Vertex(poly.v[0]).y + w1 * mesh_.Vertex(poly.v[1]).y + w2 * mesh_.Vertex(poly.v[2]).y;
            found = true;
            break;
        }
        if (!found)
        {
            // 落在多边形边界缝隙上：退回最近的多边形中心高度。
            float bestD = std::numeric_limits<float>::max();
            for (int pi : corridor)
            {
                const glm::vec2 c = NavDetail::XZ(mesh_.PolyCenter(pi));
                const float d = glm::distance(c, p);
                if (d < bestD)
                {
                    bestD = d;
                    bestY = mesh_.PolyCenter(pi).y;
                }
            }
        }
        return glm::vec3(p.x, bestY, p.y);
    }

    const NavMesh& mesh_;
};

} // namespace BigHero::Navigation
