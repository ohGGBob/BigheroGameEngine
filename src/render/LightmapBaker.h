#pragma once
// 光照贴图烘焙（Lightmap，U2-L1 的 lightmap 半部）：把静态场景接收的直接光 + 天光离线
// 烘焙进一张图集纹理，运行时按 chart 查表一次采样即得「烘焙级」光照（成本近零）。
// 纯 CPU、仅依赖 glm 与标准库、不触碰 Vulkan 对象；可离线单测。
//
// 背景与动机：
//   实时管线里 CSM 只覆盖方向光主光，点光源阴影在密度上去后开销爆炸；而静态场景的
//   光照并不逐帧变化——业界（Unity / Unreal / Quake）的通用解法是把「静态几何接收的
//   直接光 + 天光」离线烘焙成光照贴图，运行时按 chart 采样一次纹理。本模块即这条链路
//   的 CPU 核心：chart 参数化 / 图集打包 / 光线烘焙 / 运行时采样 / 文本序列化。
//
// 参数化选择（chartless，本模块最重要的一行）：
//   Unity / Unreal 的经典路径是网格 UV 展开（xatlas 类第三方库）——展开质量直接决定
//   「漏光 / seam 长尾」，这也是 U2-L1 验证列预算的大头。本模块走 chartless：每个输入
//   三角形独立正交投影成图集里一块小矩形（chart），texel 密度按世界尺寸自适应，纹理
//   空间连续性由「chart 内容区边缘钳制采样」兜底（等于把每个 chart 的边界值外扩），
//   完全绕开全局参数化。代价是图集利用率低于全局展开——对小体量场景可接受，换来的
//   是零参数化漏光长尾。这也是商业方案把 xatlas 列为网络依赖暂停项的替代路线。
//
// 光照模型（与 PBR 多光源口径一致）：
//   - 方向光：Li = color * max(dot(N, -dir), 0)（dir 为光照行进方向，从光源指向场景）。
//   - 点光源：Li = color * intensity * clamp(1 - d/radius, 0)^2 * max(dot(N, L), 0)
//     （平方衰减 + 半径窗口，与 README「最多 8 盏点光源」的衰减约定一致）。
//   - 天光：半球辐照度 skyColor * (1 + dot(up, N)) / 2，乘 AO（半球可见性）。
//     约定与 LightProbe 一致：均匀环境（各方向辐射亮度恒为 L）下水平面恒得 L。
//   - 输出为**出射辐射度（含 albedo）**：outColor = albedo * (Σ直接光 + 天光*AO)，
//     与 Unity lightmap 语义一致——运行时直接累加，不再乘 albedo。
//
// 契约：
//   - 每 chart 与输入三角形一一对应（charts_[i] 即第 i 号三角形的 chart），
//     chart 内容区 = rectSize - 2*margin（margin 为 dilation 边距，防相邻纹理渗色）；
//     内容区覆盖三角形的 (U,V) 边界盒，中心落在三角形外的浪费 texel 保持零值
//     （chartless 已知取舍：运行时采样点全在三角形内，仅长边附近半 texel 渐变区
//      可能混入零值，texel 密度越高影响越小）。
//   - 三角形约定逆时针（CCW）环绕，法线由叉积给出；退化三角形（共线/零边）判定失败。
//   - 阴影射线起点沿面法线偏移 shadowBias，命中参数 t < shadowBias 的近距命中忽略
//     （防共面邻居 / 自身假阳性）；默认不剔除背向面（单面挡板也遮挡，保守防漏光），
//     闭网格场景可开 cullBackfaces 提速。
//   - 烘焙是 O(charts x texels x lights x triangles)，属离线成本；运行时采样是 O(1)
//     + 4 次 RGBE 解码。未做线程同步：Bake 为写、Sample 为读，分别离线与运行时执行。
//   - 序列化为纯文本快照（仿 AssetDatabase::SaveCache 先例）：确定可重现、便于 diff
//     与版本控制；RGBE 编码/解码与 HdrImage::RGBEToLinear 的约定逐位兼容。
//
// 行尾/风格：LF（.gitattributes eol=lf），Allman 大括号 / 4 空格 / 120 列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/FileSystemUtils.h"

namespace BigHero::Render
{
// 烘焙参数。
struct LightmapBakeParams
{
    int atlasSize = 512;          // 图集边长（texel）
    float worldTexelSize = 0.25f; // 每 texel 对应的世界边长（texel 密度，越小越精细）
    int margin = 2;               // chart dilation 边距（texel，防相邻纹理渗色）
    float shadowBias = 0.01f;     // 阴影射线起点沿法线的偏移量（世界单位）
    int aoSamples = 8;            // AO 半球固定方向采样数（确定性，0 = 禁用 AO）
    glm::vec3 skyColor{0.4f};     // 半球天光辐亮度（均匀环境值）
    bool bakeSky = true;          // 是否烘焙天光分量
    // 阴影射线是否跳过「面法线与射线同向」的三角形。默认 false（保守）：纸片式单面
    // 几何（墙壁/挡板/浮板）无论哪一面朝向射线都参与遮挡，宁可多遮不错遮；true 仅适合
    // 严格双面闭合的网格（可省掉体内部的远端命中，但单面挡板会漏光）。
    bool cullBackfaces = false;
};

// 烘焙输入三角形（世界空间）。
struct LightmapTri
{
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    glm::vec3 c{0.0f};
    glm::vec3 albedo{1.0f}; // 漫反射反照率（烘焙进出射辐射度）
    bool castShadow = true; // 是否参与遮挡（纯接收体设为 false）
};

// 方向光描述（dir 为光照行进方向，无需归一化，烘焙时内部归一）。
struct DirectionalLightDesc
{
    glm::vec3 dir{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
};

// 点光源描述（平方衰减 + 半径窗口）。
struct PointLightDesc
{
    glm::vec3 pos{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float radius = 10.0f;
};

// 图集内一个 chart（与输入三角形一一对应）。
struct LightmapChart
{
    glm::uvec2 rectPos{0u, 0u};    // 图集内矩形左下角（texel）
    glm::uvec2 rectSize{0u, 0u};   // 矩形尺寸（含 margin）
    glm::uvec2 contentSize{0u, 0u}; // 内容区尺寸（rectSize - 2*margin）
    glm::vec3 originWorld{0.0f};   // 内容区左下角 texel **角点**（非中心）的世界坐标
    glm::vec3 axisU{1.0f, 0.0f, 0.0f}; // 世界空间 U 轴（单位、沿最长边）
    glm::vec3 axisV{0.0f, 0.0f, 1.0f}; // 世界空间 V 轴（单位、平面内垂直）
    float texelSize = 1.0f;        // 本 chart 的 texel 世界边长
    int triIndex = 0;              // 对应输入三角形的下标
};

// 烘焙结果。
struct LightmapResult
{
    int atlasSize = 0;
    float worldTexelSize = 0.0f;
    std::vector<uint8_t> texelsRgbe; // atlasSize x atlasSize x 4，行主序
    std::vector<LightmapChart> charts; // charts[i] 即第 i 号三角形的 chart
};

namespace detail
{
// ---- RGBE（Radiance 约定），与 HdrImage::RGBEToLinear 逐位兼容 ----

// 线性 RGB → RGBE（e 为 2 的共享指数，decode 约定 v = rgb * 2^(e-136)）。
inline void RgbeEncode(const glm::vec3& linear, uint8_t out[4])
{
    const float m = std::max(linear.x, std::max(linear.y, linear.z));
    if (!(m > 0.0f) || m < 1e-32f)
    {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    int fexp = 0;
    std::frexp(m, &fexp); // m = f * 2^fexp，f ∈ [0.5, 1)
    const int e = fexp + 128;
    if (e < 1)
    {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    const float scale = std::ldexp(1.0f, std::min(e, 255) - 136);
    const float v[3] = {linear.x / scale, linear.y / scale, linear.z / scale};
    for (int i = 0; i < 3; ++i)
    {
        const long r = std::lround(v[i]);
        out[i] = static_cast<uint8_t>(std::max(0L, std::min(255L, r)));
    }
    out[3] = static_cast<uint8_t>(std::min(e, 255));
}

inline glm::vec3 RgbeDecode(const uint8_t rgbe[4])
{
    if (rgbe[3] == 0)
        return glm::vec3(0.0f);
    const float scale = std::ldexp(1.0f, static_cast<int>(rgbe[3]) - 128 - 8);
    return glm::vec3(rgbe[0], rgbe[1], rgbe[2]) * scale;
}

// 按 atlas 坐标读/写一个 texel（行主序）。
inline void ReadTexel(const LightmapResult& lm, int x, int y, glm::vec3& out)
{
    const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(lm.atlasSize) + static_cast<size_t>(x)) * 4u;
    out = RgbeDecode(&lm.texelsRgbe[idx]);
}

inline void WriteTexel(LightmapResult& lm, int x, int y, const glm::vec3& v)
{
    const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(lm.atlasSize) + static_cast<size_t>(x)) * 4u;
    RgbeEncode(v, &lm.texelsRgbe[idx]);
}

// Möller–Trumbore（双精度，避免大坐标下浮点 |det| 误判）。命中 t > 0 才返回 true。
inline bool RayTriangleHit(const glm::dvec3& o, const glm::dvec3& d, const glm::dvec3& a, const glm::dvec3& b,
                           const glm::dvec3& c, double& tOut)
{
    const glm::dvec3 e1 = b - a;
    const glm::dvec3 e2 = c - a;
    const glm::dvec3 p = glm::cross(d, e2);
    const double det = glm::dot(e1, p);
    if (std::fabs(det) < 1e-12)
        return false;
    const double inv = 1.0 / det;
    const glm::dvec3 tv = o - a;
    const double u = glm::dot(tv, p) * inv;
    if (u < 0.0 || u > 1.0)
        return false;
    const glm::dvec3 q = glm::cross(tv, e1);
    const double v = glm::dot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0)
        return false;
    const double t = glm::dot(e2, q) * inv;
    if (t <= 0.0)
        return false;
    tOut = t;
    return true;
}

// 从 origin 沿 dir（单位）打一条阴影射线到 maxT：是否被任何三角形挡住。
// selfIndex：自身所在三角形跳过；t < minHitT 的近距命中（共面邻居）忽略。
inline bool RayBlocked(const glm::dvec3& origin, const glm::dvec3& dir, double maxT, double minHitT,
                       const std::vector<LightmapTri>& tris, const LightmapBakeParams& params, int selfIndex)
{
    double best = std::numeric_limits<double>::max();
    for (size_t j = 0; j < tris.size(); ++j)
    {
        if (static_cast<int>(j) == selfIndex)
            continue;
        const LightmapTri& tri = tris[j];
        if (!tri.castShadow)
            continue;
        if (params.cullBackfaces)
        {
            const glm::vec3 n = glm::cross(tri.b - tri.a, tri.c - tri.a);
            if (glm::dot(n, glm::vec3(dir)) >= 0.0f)
                continue; // 背向射线的面不遮挡
        }
        double t = 0.0;
        if (RayTriangleHit(origin, dir, glm::dvec3(tri.a), glm::dvec3(tri.b), glm::dvec3(tri.c), t))
            best = std::min(best, t);
    }
    return best >= minHitT && best < maxT;
}

// 确定性 AO 方向集：球面 Fibonacci 点列（与 test_light_probe 的验证驱动同构）取上半球前 count 个。
inline std::vector<glm::vec3> AoSampleDirs(const glm::vec3& normal, int count)
{
    std::vector<glm::vec3> out;
    out.reserve(static_cast<size_t>(count));
    constexpr float kGolden = 2.39996322972865332f; // π(3-√5)
    const int probes = std::max(count * 4, 16);
    for (int k = 0; k < probes && static_cast<int>(out.size()) < count; ++k)
    {
        const float z = 1.0f - (2.0f * static_cast<float>(k) + 1.0f) / static_cast<float>(probes);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float phi = kGolden * static_cast<float>(k);
        const glm::vec3 d(r * std::cos(phi), z, r * std::sin(phi));
        if (glm::dot(d, normal) > 0.05f)
            out.push_back(glm::normalize(d));
    }
    return out;
}

// 由三角形构造 chart（正交投影参数化）。退化的（共线/零边）返回 false。
inline bool BuildChart(const LightmapTri& tri, const LightmapBakeParams& params, LightmapChart& chart)
{
    const glm::vec3 e0 = tri.b - tri.a;
    const glm::vec3 e1 = tri.c - tri.a;
    const float l0 = glm::length(e0);
    const float l1 = glm::length(e1);
    if (l0 < 1e-8f || l1 < 1e-8f)
        return false;
    const glm::vec3 n = glm::cross(e0, e1);
    const float nl = glm::length(n);
    if (nl < 1e-10f)
        return false;

    // 最长边为 U 轴（贴近三角形主方向），V = n × U 保证 (U, V, n) 右手正交。
    chart.axisU = (l0 >= l1) ? e0 / l0 : e1 / l1;
    chart.axisV = glm::normalize(glm::cross(n / nl, chart.axisU));

    // 顶点在 (U, V) 帧的**绝对**投影范围（不是相对 a 的增量投影），
    // 原点取绝对最小角点：内容区 [0,wu]×[0,wv] 恰好紧裹三角形。
    const float pa[2] = {glm::dot(tri.a, chart.axisU), glm::dot(tri.a, chart.axisV)};
    const float pb[2] = {glm::dot(tri.b, chart.axisU), glm::dot(tri.b, chart.axisV)};
    const float pc[2] = {glm::dot(tri.c, chart.axisU), glm::dot(tri.c, chart.axisV)};
    const float minU = std::min(pa[0], std::min(pb[0], pc[0]));
    const float maxU = std::max(pa[0], std::max(pb[0], pc[0]));
    const float minV = std::min(pa[1], std::min(pb[1], pc[1]));
    const float maxV = std::max(pa[1], std::max(pb[1], pc[1]));
    const float wu = maxU - minU;
    const float wv = maxV - minV;
    if (wu < 1e-8f || wv < 1e-8f)
        return false;

    chart.originWorld = chart.axisU * minU + chart.axisV * minV;

    const int cu = std::max(1, static_cast<int>(std::ceil(wu / params.worldTexelSize)));
    const int cv = std::max(1, static_cast<int>(std::ceil(wv / params.worldTexelSize)));

    chart.rectPos = glm::uvec2(0u, 0u);
    chart.contentSize = glm::uvec2(static_cast<unsigned>(cu), static_cast<unsigned>(cv));
    chart.rectSize = chart.contentSize + glm::uvec2(static_cast<unsigned>(params.margin * 2));
    chart.texelSize = params.worldTexelSize;
    return true;
}

// 点是否在三角形内（含缩进容差）：重心坐标带符号面积判定，权重 < -tol 即在外。
// 用于跳过「texel 中心落在三角形外」的内容区 texel（chartless 的浪费区，写入零值）。
inline bool PointInTri(const glm::vec3& p, const LightmapTri& tri)
{
    const glm::vec3 n = glm::cross(tri.b - tri.a, tri.c - tri.a);
    const float nn = glm::dot(n, n);
    if (nn < 1e-20f)
        return false;
    const float tol = 1e-4f * nn;
    const float wa = glm::dot(glm::cross(tri.b - p, tri.c - p), n);
    const float wb = glm::dot(glm::cross(tri.c - p, tri.a - p), n);
    const float wc = glm::dot(glm::cross(tri.a - p, tri.b - p), n);
    return wa >= -tol && wb >= -tol && wc >= -tol;
}

// Guillotine 图集打包：按「高度降序 → 宽度降序 → 三角形下标升序」逐块放置，
// 扫描序 = 自由矩形插入序，全程确定可重现。放不下返回 false。
inline bool PackAtlas(std::vector<LightmapChart>& charts, const LightmapBakeParams& params)
{
    struct FreeRect
    {
        int x;
        int y;
        int w;
        int h;
    };
    std::vector<size_t> order(charts.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&charts](size_t ia, size_t ib) {
        const LightmapChart& a = charts[ia];
        const LightmapChart& b = charts[ib];
        if (a.rectSize.y != b.rectSize.y)
            return a.rectSize.y > b.rectSize.y;
        if (a.rectSize.x != b.rectSize.x)
            return a.rectSize.x > b.rectSize.x;
        return ia < ib;
    });

    std::vector<FreeRect> free;
    free.push_back(FreeRect{0, 0, params.atlasSize, params.atlasSize});
    for (const size_t idx : order)
    {
        LightmapChart& chart = charts[idx];
        const int cw = static_cast<int>(chart.rectSize.x);
        const int ch = static_cast<int>(chart.rectSize.y);
        size_t fit = free.size();
        for (size_t f = 0; f < free.size(); ++f)
        {
            if (free[f].w >= cw && free[f].h >= ch)
            {
                fit = f;
                break;
            }
        }
        if (fit == free.size())
            return false; // 图集放不下：调用方应调大 atlasSize 或减小几何
        const FreeRect fr = free[fit];
        chart.rectPos = glm::uvec2(static_cast<unsigned>(fr.x), static_cast<unsigned>(fr.y));
        free.erase(free.begin() + static_cast<long>(fit));
        // 切分剩余空间：右侧条（保持 chart 高）与上侧条（保持原宽），先右后上，确定性。
        if (fr.w > cw)
            free.push_back(FreeRect{fr.x + cw, fr.y, fr.w - cw, ch});
        if (fr.h > ch)
            free.push_back(FreeRect{fr.x, fr.y + ch, fr.w, fr.h - ch});
    }
    return true;
}

// 文本快照序列化用：float 以 %.9g 输出（float32 十进制往返精确保真）。
inline std::string FmtFloat(float v)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return buf;
}

inline std::string HexByte(unsigned v)
{
    static const char* kHex = "0123456789ABCDEF";
    std::string s;
    s.push_back(kHex[(v >> 4) & 0xFu]);
    s.push_back(kHex[v & 0xFu]);
    return s;
}

inline int HexVal(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
} // namespace detail

// ---- 公共 API ----

// RGBE 编解码（与 HdrImage::RGBEToLinear 约定逐位兼容）。
inline void RgbeEncode(const glm::vec3& linear, uint8_t out[4]) { detail::RgbeEncode(linear, out); }
inline glm::vec3 RgbeDecode(const uint8_t rgbe[4]) { return detail::RgbeDecode(rgbe); }

// 烘焙整张光照贴图。tris 为静态几何，dirs/points 为光源；成功返回 true 并填 out。
// 任一条目退化或图集放不下时返回 false 且 out 置空。
inline bool BakeLightmap(const std::vector<LightmapTri>& tris, const std::vector<DirectionalLightDesc>& dirs,
                         const std::vector<PointLightDesc>& points, const LightmapBakeParams& params,
                         LightmapResult& out)
{
    out = LightmapResult{};
    if (tris.empty() || params.atlasSize < 16 || !(params.worldTexelSize > 0.0f) || params.margin < 1)
        return false;

    std::vector<LightmapChart> charts(tris.size());
    for (size_t i = 0; i < tris.size(); ++i)
    {
        if (!detail::BuildChart(tris[i], params, charts[i]))
            return false;
        charts[i].triIndex = static_cast<int>(i);
    }
    if (!detail::PackAtlas(charts, params))
        return false;

    out.atlasSize = params.atlasSize;
    out.worldTexelSize = params.worldTexelSize;
    out.texelsRgbe.assign(
        static_cast<size_t>(params.atlasSize) * static_cast<size_t>(params.atlasSize) * 4u, 0u);
    out.charts = charts;

    // 归一化过的光源方向（避免每 texel 重复 normalize）。
    std::vector<glm::vec3> dirDirs(dirs.size());
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        const float l = glm::length(dirs[i].dir);
        dirDirs[i] = l > 1e-8f ? dirs[i].dir / l : glm::vec3(0.0f, -1.0f, 0.0f);
    }

    const float tMinHit = params.shadowBias;
    for (size_t ci = 0; ci < out.charts.size(); ++ci)
    {
        const LightmapChart& ch = out.charts[ci];
        const LightmapTri& tri = tris[ci];
        const glm::vec3 faceN = glm::normalize(glm::cross(tri.b - tri.a, tri.c - tri.a));
        const glm::vec3 skyUp(0.0f, 1.0f, 0.0f);
        const std::vector<glm::vec3> aoDirs =
            params.bakeSky && params.aoSamples > 0 ? detail::AoSampleDirs(faceN, params.aoSamples)
                                                   : std::vector<glm::vec3>{};
        const float skyFactor = (1.0f + glm::dot(skyUp, faceN)) * 0.5f;

        for (unsigned ty = 0; ty < ch.contentSize.y; ++ty)
        {
            for (unsigned tx = 0; tx < ch.contentSize.x; ++tx)
            {
                // texel 中心 → 世界坐标（内容区角点 + 半 texel 偏移）。
                const glm::vec3 local(static_cast<float>(tx) + 0.5f, static_cast<float>(ty) + 0.5f, 0.0f);
                const glm::vec3 world = ch.originWorld + ch.axisU * (local.x * ch.texelSize) +
                                        ch.axisV * (local.y * ch.texelSize);
                if (!detail::PointInTri(world, tri))
                    continue; // 中心在三角形外的浪费 texel：保持零值（chartless 已知取舍）
                const glm::vec3 biased = world + faceN * params.shadowBias;

                glm::vec3 lit(0.0f);
                for (size_t li = 0; li < dirs.size(); ++li)
                {
                    const float dotNL = -glm::dot(faceN, dirDirs[li]);
                    if (dotNL <= 0.0f)
                        continue;
                    const bool blocked = detail::RayBlocked(
                        glm::dvec3(biased), glm::dvec3(-dirDirs[li]),
                        std::numeric_limits<double>::max(), tMinHit, tris, params, static_cast<int>(ci));
                    if (!blocked)
                        lit += dirs[li].color * dotNL;
                }
                for (size_t pi = 0; pi < points.size(); ++pi)
                {
                    const glm::vec3 toLight = points[pi].pos - world;
                    const float dist = glm::length(toLight);
                    if (dist < 1e-6f || dist >= points[pi].radius)
                        continue;
                    const glm::vec3 ldir = toLight / dist;
                    const float dotNL = glm::dot(faceN, ldir);
                    if (dotNL <= 0.0f)
                        continue;
                    const bool blocked =
                        detail::RayBlocked(glm::dvec3(biased), glm::dvec3(ldir), static_cast<double>(dist), tMinHit,
                                           tris, params, static_cast<int>(ci));
                    if (!blocked)
                    {
                        const float fall = 1.0f - dist / points[pi].radius;
                        lit += points[pi].color * (points[pi].intensity * fall * fall * dotNL);
                    }
                }
                if (params.bakeSky && params.skyColor != glm::vec3(0.0f))
                {
                    float ao = 1.0f;
                    if (!aoDirs.empty())
                    {
                        int unocc = 0;
                        for (const glm::vec3& d : aoDirs)
                        {
                            if (!detail::RayBlocked(glm::dvec3(biased), glm::dvec3(d),
                                                    std::numeric_limits<double>::max(), tMinHit, tris, params,
                                                    static_cast<int>(ci)))
                                ++unocc;
                        }
                        ao = static_cast<float>(unocc) / static_cast<float>(aoDirs.size());
                    }
                    lit += params.skyColor * (skyFactor * ao);
                }

                const glm::vec3 folded = tri.albedo * lit;
                const int ax = static_cast<int>(ch.rectPos.x) + params.margin + static_cast<int>(tx);
                const int ay = static_cast<int>(ch.rectPos.y) + params.margin + static_cast<int>(ty);
                detail::WriteTexel(out, ax, ay, folded);
            }
        }
    }
    return true;
}

// 运行时采样（第二阶段接线用）：由世界坐标在 chart 内容区内双线性过滤，
// 超出内容区按边缘钳制（dilation 外扩，防 chart 边界漏光）。返回出射辐射度。
inline glm::vec3 SampleLightmap(const LightmapResult& lm, int chartIndex, const glm::vec3& worldPos)
{
    if (chartIndex < 0 || chartIndex >= static_cast<int>(lm.charts.size()))
        return glm::vec3(0.0f);
    const LightmapChart& c = lm.charts[chartIndex];
    const glm::vec3 rel = worldPos - c.originWorld;
    const float gx = glm::dot(rel, c.axisU) / c.texelSize - 0.5f;
    const float gy = glm::dot(rel, c.axisV) / c.texelSize - 0.5f;
    const float maxX = static_cast<float>(c.contentSize.x) - 1.0f;
    const float maxY = static_cast<float>(c.contentSize.y) - 1.0f;
    const float sx = std::clamp(gx, 0.0f, maxX);
    const float sy = std::clamp(gy, 0.0f, maxY);
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const int x1 = std::min(x0 + 1, static_cast<int>(c.contentSize.x) - 1);
    const int y1 = std::min(y0 + 1, static_cast<int>(c.contentSize.y) - 1);
    const float fx = sx - static_cast<float>(x0);
    const float fy = sy - static_cast<float>(y0);

    const unsigned marginX = (c.rectSize.x - c.contentSize.x) / 2u;
    const unsigned marginY = (c.rectSize.y - c.contentSize.y) / 2u;
    const int ox = static_cast<int>(c.rectPos.x) + static_cast<int>(marginX);
    const int oy = static_cast<int>(c.rectPos.y) + static_cast<int>(marginY);
    glm::vec3 c00, c10, c01, c11;
    detail::ReadTexel(lm, ox + x0, oy + y0, c00);
    detail::ReadTexel(lm, ox + x1, oy + y0, c10);
    detail::ReadTexel(lm, ox + x0, oy + y1, c01);
    detail::ReadTexel(lm, ox + x1, oy + y1, c11);
    return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
}

// 序列化：纯文本快照（可重现、便于 diff）。返回是否成功（写失败 / 数据不合法）。
inline bool SaveLightmap(const LightmapResult& lm, const std::string& path)
{
    if (lm.atlasSize < 1 || lm.texelsRgbe.size() !=
                                static_cast<size_t>(lm.atlasSize) * static_cast<size_t>(lm.atlasSize) * 4u)
        return false;
    std::ostringstream ss;
    ss << "bighero-lightmap 1\n";
    ss << "size " << lm.atlasSize << "\n";
    ss << "texel " << detail::FmtFloat(lm.worldTexelSize) << "\n";
    ss << "charts " << lm.charts.size() << "\n";
    for (size_t i = 0; i < lm.charts.size(); ++i)
    {
        const LightmapChart& c = lm.charts[i];
        ss << "chart " << i << " rect " << c.rectPos.x << " " << c.rectPos.y << " " << c.rectSize.x << " "
           << c.rectSize.y << " content " << c.contentSize.x << " " << c.contentSize.y << " u " << detail::FmtFloat(c.axisU.x)
           << " " << detail::FmtFloat(c.axisU.y) << " " << detail::FmtFloat(c.axisU.z) << " v " << detail::FmtFloat(c.axisV.x)
           << " " << detail::FmtFloat(c.axisV.y) << " " << detail::FmtFloat(c.axisV.z) << " origin "
           << detail::FmtFloat(c.originWorld.x) << " " << detail::FmtFloat(c.originWorld.y) << " "
           << detail::FmtFloat(c.originWorld.z) << " texel " << detail::FmtFloat(c.texelSize) << " tri "
           << c.triIndex << "\n";
    }
    ss << "data\n";
    for (const uint8_t b : lm.texelsRgbe)
        ss << detail::HexByte(b);
    ss << "\n";
    return Core::FileSystem::WriteText(path, ss.str());
}

inline bool LoadLightmap(const std::string& path, LightmapResult& lm)
{
    std::string text;
    if (!Core::FileSystem::ReadText(path, text))
        return false;
    std::istringstream ss(text);
    std::string tag;
    int version = 0;
    if (!(ss >> tag >> version) || tag != "bighero-lightmap" || version != 1)
        return false;
    LightmapResult out;
    size_t chartCount = 0;
    bool sizeSeen = false;
    std::string word;
    while (ss >> word)
    {
        if (word == "size")
        {
            if (!(ss >> out.atlasSize) || out.atlasSize < 1)
                return false;
            sizeSeen = true;
        }
        else if (word == "texel")
        {
            if (!(ss >> out.worldTexelSize))
                return false;
        }
        else if (word == "charts")
        {
            if (!(ss >> chartCount))
                return false;
            out.charts.reserve(chartCount);
        }
        else if (word == "chart")
        {
            LightmapChart c;
            size_t idx = 0;
            if (!(ss >> idx >> word) || word != "rect" || !(ss >> c.rectPos.x >> c.rectPos.y >> c.rectSize.x >>
                                                             c.rectSize.y >> word) ||
                word != "content" || !(ss >> c.contentSize.x >> c.contentSize.y >> word) || word != "u" ||
                !(ss >> c.axisU.x >> c.axisU.y >> c.axisU.z >> word) || word != "v" ||
                !(ss >> c.axisV.x >> c.axisV.y >> c.axisV.z >> word) || word != "origin" ||
                !(ss >> c.originWorld.x >> c.originWorld.y >> c.originWorld.z >> word) || word != "texel" ||
                !(ss >> c.texelSize >> word) || word != "tri" || !(ss >> c.triIndex) || idx >= chartCount)
                return false;
            out.charts.push_back(c);
        }
        else if (word == "data")
        {
            std::string hex;
            if (!(ss >> hex))
                return false;
            const size_t expected = static_cast<size_t>(out.atlasSize) * static_cast<size_t>(out.atlasSize) * 4u * 2u;
            if (!sizeSeen || hex.size() != expected)
                return false;
            out.texelsRgbe.resize(expected / 2u);
            for (size_t i = 0; i < expected / 2u; ++i)
            {
                const int hi = detail::HexVal(hex[i * 2u]);
                const int lo = detail::HexVal(hex[i * 2u + 1u]);
                if (hi < 0 || lo < 0)
                    return false;
                out.texelsRgbe[i] = static_cast<uint8_t>((static_cast<unsigned>(hi) << 4u) | static_cast<unsigned>(lo));
            }
            break;
        }
        else
        {
            return false; // 未知字段：视为格式损坏，整体拒绝
        }
    }
    if (out.charts.size() != chartCount || out.texelsRgbe.empty())
        return false;
    lm = out;
    return true;
}

} // namespace BigHero::Render