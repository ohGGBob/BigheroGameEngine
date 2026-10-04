// 光照贴图烘焙（render/LightmapBaker.h）：纯逻辑、零 GPU，可离线运行。
// 覆盖图集打包（边界/不重叠/边距）、方向光解析解、albedo 吸收、点光源平方衰减解析解、
// 硬阴影、AO 贴墙 vs 开阔单调性、双线性过滤已知值、多光源求和、退化输入拒绝、
// RGBE 量化误差、文本序列化往返与损坏输入拒绝、重复烘焙确定性。
#include "framework/test_common.h"

#include "render/LightmapBaker.h"

#include <filesystem>

using namespace BigHero;
using BigHero::Render::BakeLightmap;
using BigHero::Render::BuildStaticLightmapBatch;
using BigHero::Render::DirectionalLightDesc;
using BigHero::Render::LightmapBakeParams;
using BigHero::Render::LightmapBatchVertex;
using BigHero::Render::LightmapChart;
using BigHero::Render::LightmapResult;
using BigHero::Render::LightmapStaticBatch;
using BigHero::Render::LightmapTri;
using BigHero::Render::LoadLightmap;
using BigHero::Render::PointLightDesc;
using BigHero::Render::RgbeDecode;
using BigHero::Render::RgbeEncode;
using BigHero::Render::SampleLightmap;
using BigHero::Render::SaveLightmap;

namespace
{
// 地面四分体（y=0，x/z ∈ [-8,8]，逆时针绕序、法线 +Y），
// 沿 z=x 对角线拆成两个直角三角形（chartless 每个三角形一块 chart）。
// 划分：z >= x 归 tri0，z < x 归 tri1。
void MakeFloor(std::vector<LightmapTri>& tris, const glm::vec3& albedo = glm::vec3(1.0f))
{
    LightmapTri t0;
    t0.a = glm::vec3(-8.0f, 0.0f, -8.0f);
    t0.b = glm::vec3(-8.0f, 0.0f, 8.0f);
    t0.c = glm::vec3(8.0f, 0.0f, 8.0f);
    t0.albedo = albedo;
    LightmapTri t1;
    t1.a = glm::vec3(-8.0f, 0.0f, -8.0f);
    t1.b = glm::vec3(8.0f, 0.0f, 8.0f);
    t1.c = glm::vec3(8.0f, 0.0f, -8.0f);
    t1.albedo = albedo;
    tris.push_back(t0);
    tris.push_back(t1);
}

// 地面采样点所属 chart（远离对角线才可调用）。
int FloorChart(const glm::vec3& p)
{
    return p.z > p.x + 1e-3f ? 0 : 1;
}

// 遮挡板（y=0.2，x/z ∈ [-3,3]，法线 +Y，向下的方向光会在地面投硬阴影）。
void MakeOccluder(std::vector<LightmapTri>& tris)
{
    LightmapTri o0;
    o0.a = glm::vec3(-3.0f, 0.2f, -3.0f);
    o0.b = glm::vec3(-3.0f, 0.2f, 3.0f);
    o0.c = glm::vec3(3.0f, 0.2f, 3.0f);
    LightmapTri o1;
    o1.a = glm::vec3(-3.0f, 0.2f, -3.0f);
    o1.b = glm::vec3(3.0f, 0.2f, 3.0f);
    o1.c = glm::vec3(3.0f, 0.2f, -3.0f);
    tris.push_back(o0);
    tris.push_back(o1);
}

// 竖直墙（x=-1 平面，y ∈ [0,4]，z ∈ [-4,4]，法线 +X，用于贴地遮挡 AO 侧向射线）。
void MakeWall(std::vector<LightmapTri>& tris)
{
    LightmapTri w0;
    w0.a = glm::vec3(-1.0f, 0.0f, -4.0f);
    w0.b = glm::vec3(-1.0f, 4.0f, 4.0f);
    w0.c = glm::vec3(-1.0f, 0.0f, 4.0f);
    LightmapTri w1;
    w1.a = glm::vec3(-1.0f, 0.0f, -4.0f);
    w1.b = glm::vec3(-1.0f, 4.0f, -4.0f);
    w1.c = glm::vec3(-1.0f, 4.0f, 4.0f);
    tris.push_back(w0);
    tris.push_back(w1);
}

// 三通道容差断言。
void CheckVecNear(const glm::vec3& v, float x, float y, float z, float eps)
{
    CHECK_NEAR(v.x, x, eps);
    CHECK_NEAR(v.y, y, eps);
    CHECK_NEAR(v.z, z, eps);
}

// 图集打包：所有 chart 在边界内、两两不重叠、rectSize = content + 2*margin。
// 同时校验重复烘焙的确定性（白盒打包序）。
TEST_CASE("Lightmap.AtlasPack")
{
    std::vector<LightmapTri> tris;
    MakeFloor(tris);
    MakeOccluder(tris);
    MakeWall(tris);
    LightmapBakeParams params;
    params.atlasSize = 128;
    params.worldTexelSize = 0.5f;
    params.margin = 2;
    params.bakeSky = false;
    std::vector<DirectionalLightDesc> dirs;
    dirs.push_back(DirectionalLightDesc{glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f)});
    std::vector<PointLightDesc> points;

    LightmapResult lm;
    CHECK(BakeLightmap(tris, dirs, points, params, lm));
    CHECK_EQ(lm.charts.size(), tris.size());
    CHECK_EQ(lm.atlasSize, 128);

    bool overlap = false;
    bool outOfBounds = false;
    bool marginOk = true;
    for (size_t i = 0; i < lm.charts.size(); ++i)
    {
        const LightmapChart& a = lm.charts[i];
        if (a.rectPos.x + a.rectSize.x > static_cast<unsigned>(lm.atlasSize) ||
            a.rectPos.y + a.rectSize.y > static_cast<unsigned>(lm.atlasSize))
            outOfBounds = true;
        if (a.rectSize.x != a.contentSize.x + 2u * static_cast<unsigned>(params.margin) ||
            a.rectSize.y != a.contentSize.y + 2u * static_cast<unsigned>(params.margin))
            marginOk = false;
        CHECK_EQ(a.triIndex, static_cast<int>(i));
        for (size_t j = i + 1; j < lm.charts.size(); ++j)
        {
            const LightmapChart& b = lm.charts[j];
            const bool sepX = a.rectPos.x + a.rectSize.x <= b.rectPos.x || b.rectPos.x + b.rectSize.x <= a.rectPos.x;
            const bool sepY = a.rectPos.y + a.rectSize.y <= b.rectPos.y || b.rectPos.y + b.rectSize.y <= a.rectPos.y;
            if (!sepX && !sepY)
                overlap = true;
        }
    }
    CHECK(!overlap);
    CHECK(!outOfBounds);
    CHECK(marginOk);

    LightmapResult lm2;
    CHECK(BakeLightmap(tris, dirs, points, params, lm2));
    CHECK_EQ(lm2.texelsRgbe.size(), lm.texelsRgbe.size());
    CHECK_EQ(lm2.texelsRgbe, lm.texelsRgbe); // 逐字节一致 = 白盒确定
    CHECK_EQ(lm2.charts.size(), lm.charts.size());
}

// 方向光解析解：水平面 + 垂直向下方向光 → texel = color * albedo（无遮挡、无天光）。
TEST_CASE("Lightmap.DirectionalAnalytic")
{
    std::vector<LightmapTri> tris;
    MakeFloor(tris);
    LightmapBakeParams params;
    params.atlasSize = 256;
    params.worldTexelSize = 0.5f;
    params.bakeSky = false;
    std::vector<DirectionalLightDesc> dirs;
    dirs.push_back(DirectionalLightDesc{glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.2f)});
    std::vector<PointLightDesc> points;
    LightmapResult lm;
    CHECK(BakeLightmap(tris, dirs, points, params, lm));
    const glm::vec3 p0(-4.0f, 0.0f, -4.5f); // tri1
    const glm::vec3 p1(-2.0f, 0.0f, -0.5f); // tri0
    const glm::vec3 s0 = SampleLightmap(lm, FloorChart(p0), p0);
    const glm::vec3 s1 = SampleLightmap(lm, FloorChart(p1), p1);
    CheckVecNear(s0, 1.0f, 0.5f, 0.2f, 0.01f);
    CheckVecNear(s1, 1.0f, 0.5f, 0.2f, 0.01f);
}

// albedo 吸收：向下的白光 × albedo 0.5 → 出射 0.5。
TEST_CASE("Lightmap.AlbedoAbsorption")
{
    std::vector<LightmapTri> tris;
    MakeFloor(tris, glm::vec3(0.5f));
    LightmapBakeParams params;
    params.atlasSize = 256;
    params.worldTexelSize = 0.5f;
    params.bakeSky = false;
    std::vector<DirectionalLightDesc> dirs;
    dirs.push_back(DirectionalLightDesc{glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f)});
    std::vector<PointLightDesc> points;
    LightmapResult lm;
    CHECK(BakeLightmap(tris, dirs, points, params, lm));
    const glm::vec3 p(1.0f, 0.0f, 2.0f); // tri0
    CheckVecNear(SampleLightmap(lm, FloorChart(p), p), 0.5f, 0.5f, 0.5f, 0.01f);
}

// 点光源平方衰减 + 半径窗口：与 PBR 口径一致的解析解核对。
TEST_CASE("Lightmap.PointLightFalloff")
{
    std::vector<LightmapTri> tris;
    MakeFloor(tris);
    LightmapBakeParams params;
    params.atlasSize = 256;
    params.worldTexelSize = 0.5f;
    params.bakeSky = false;
    std::vector<DirectionalLightDesc> dirs;
    std::vector<PointLightDesc> points;
    PointLightDesc pl;
    pl.pos = glm::vec3(0.0f, 2.0f, 0.0f);
    pl.color = glm::vec3(0.0f, 0.0f, 1.0f);
    pl.intensity = 1.0f;
    pl.radius = 5.0f;
    points.push_back(pl);
    LightmapResult lm;
    CHECK(BakeLightmap(tris, dirs, points, params, lm));

    const glm::vec3 p(1.0f, 0.0f, 0.5f); // tri1
    const glm::vec3 toLight = pl.pos - p;
    const float d = glm::length(toLight);
    const glm::vec3 ldir = toLight / d;
    const float fall = 1.0f - d / pl.radius;
    const float expected = pl.intensity * fall * fall * glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), ldir);
    CHECK(expected > 0.2f && expected < 0.3f); // 解析值应落在 0.256 附近，防测试公式写错
    CHECK_NEAR(SampleLightmap(lm, FloorChart(p), p).z, expected, 0.01f);
}

// 硬阴影：遮挡板下方向光被挡 → 地面 texel 全黑；板外正常受光。
TEST_CASE("Lightmap.Shadows")
{
    std::vector<LightmapTri> tris;
    MakeFloor(tris);
    MakeOccluder(tris);
    LightmapBakeParams params;
    params.atlasSize = 256;
    params.worldTexelSize = 0.5f;
    params.bakeSky = false;
    std::vector<DirectionalLightDesc> dirs;
    dirs.push_back(DirectionalLightDesc{glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f)});
    std::vector<PointLightDesc> points;
    LightmapResult lm;
    CHECK(BakeLightmap(tris, dirs, points, params, lm));

    const glm::vec3 under(0.25f, 0.0f, -0.5f); // 板下（tri1），远离阴影边缘
    const glm::vec3 lit(-6.0f, 0.0f, -6.5f);   // 板外（tri1）
    const glm::vec3 sv = SampleLightmap(lm, FloorChart(under), under);
    const glm::vec3 sl = SampleLightmap(lm, FloorChart(lit), lit);
    CHECK(sv.x < 0.001f && sv.y < 0.001f && sv.z < 0.001f);
    CheckVecNear(sl, 1.0f, 1.0f, 1.0f, 0.02f);
}

// 天光 + AO：开阔地面恒得 skyColor（水平面半球因子 = 1）；贴墙处 AO 显著低于开阔处。
TEST_CASE("Lightmap.AoSky")
{
    auto bake = [](const std::vector<LightmapTri>& tris, LightmapResult& lm)
    {
        LightmapBakeParams params;
        params.atlasSize = 256;
        params.worldTexelSize = 0.5f;
        params.skyColor = glm::vec3(1.0f);
        params.bakeSky = true;
        params.aoSamples = 8;
        return BakeLightmap(tris, std::vector<DirectionalLightDesc>{}, std::vector<PointLightDesc>{}, params, lm);
    };

    std::vector<LightmapTri> openTris;
    MakeFloor(openTris);
    LightmapResult openLm;
    CHECK(bake(openTris, openLm));
    const glm::vec3 p(-4.0f, 0.0f, -4.5f); // tri1
    const float openVal = SampleLightmap(openLm, FloorChart(p), p).x;

    std::vector<LightmapTri> wallTris;
    MakeFloor(wallTris);
    MakeWall(wallTris);
    LightmapResult wallLm;
    CHECK(bake(wallTris, wallLm));
    const glm::vec3 nearWall(-0.75f, 0.0f, -0.5f); // tri0，贴墙处
    const float nearVal = SampleLightmap(wallLm, FloorChart(nearWall), nearWall).x;
    const float farVal = SampleLightmap(wallLm, FloorChart(p), p).x; // 同场景远处

    CHECK_NEAR(openVal, 1.0f, 0.02f); // 水平面无遮挡 + 均匀天光 → 恒 L
    CHECK_NEAR(farVal, 1.0f, 0.15f);  // 远处几乎不受墙影响
    CHECK(nearVal < farVal - 0.2f);   // 贴墙 AO 显著衰减
}

// 多光源求和：方向光 + 点光源独立贡献相加（无遮挡场景各自按解析式）。
TEST_CASE("Lightmap.MultiLight")
{
    std::vector<LightmapTri> tris;
    MakeFloor(tris);
    LightmapBakeParams params;
    params.atlasSize = 256;
    params.worldTexelSize = 0.5f;
    params.bakeSky = false;
    std::vector<DirectionalLightDesc> dirs;
    dirs.push_back(DirectionalLightDesc{glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)});
    std::vector<PointLightDesc> points;
    PointLightDesc pl;
    pl.pos = glm::vec3(0.0f, 2.0f, 0.0f);
    pl.color = glm::vec3(0.0f, 0.0f, 1.0f);
    pl.intensity = 1.0f;
    pl.radius = 5.0f;
    points.push_back(pl);
    LightmapResult lm;
    CHECK(BakeLightmap(tris, dirs, points, params, lm));

    const glm::vec3 near(1.0f, 0.0f, 0.5f); // tri1：方向 + 点光
    const glm::vec3 toLight = pl.pos - near;
    const float d = glm::length(toLight);
    const glm::vec3 ldir = toLight / d;
    const float fall = 1.0f - d / pl.radius;
    const float expectedPoint = fall * fall * glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), ldir);
    const glm::vec3 sn = SampleLightmap(lm, FloorChart(near), near);
    CheckVecNear(sn, 1.0f, 0.0f, expectedPoint, 0.01f);

    const glm::vec3 far(-6.0f, 0.0f, -6.5f); // tri1：点光源半径外 → 只剩方向光
    const glm::vec3 sf = SampleLightmap(lm, FloorChart(far), far);
    CheckVecNear(sf, 1.0f, 0.0f, 0.0f, 0.01f);
}

// 双线性过滤：手工构造 4x4 图集（gradient = x + 2y），核对角点/中心/中点/钳制/越界索引。
TEST_CASE("Lightmap.BilinearFilter")
{
    LightmapResult lm;
    lm.atlasSize = 16;
    lm.worldTexelSize = 1.0f;
    lm.texelsRgbe.assign(16u * 16u * 4u, 0u);
    LightmapChart c;
    c.rectPos = glm::uvec2(0u, 0u);
    c.rectSize = glm::uvec2(4u, 4u);
    c.contentSize = glm::uvec2(4u, 4u);
    c.originWorld = glm::vec3(0.0f, 0.0f, 0.0f);
    c.axisU = glm::vec3(1.0f, 0.0f, 0.0f);
    c.axisV = glm::vec3(0.0f, 0.0f, 1.0f);
    c.texelSize = 1.0f;
    c.triIndex = 0;
    lm.charts.push_back(c);
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const float v = static_cast<float>(x) + 2.0f * static_cast<float>(y);
            const size_t idx = (static_cast<size_t>(y) * 16u + static_cast<size_t>(x)) * 4u;
            RgbeEncode(glm::vec3(v), &lm.texelsRgbe[idx]);
        }
    }
    // 世界点 (x+0.5, 0, z+0.5) 恰为 texel (x,z) 中心。
    CheckVecNear(SampleLightmap(lm, 0, glm::vec3(0.5f, 0.0f, 0.5f)), 0.0f, 0.0f, 0.0f, 0.001f);
    CheckVecNear(SampleLightmap(lm, 0, glm::vec3(1.5f, 0.0f, 1.5f)), 3.0f, 3.0f, 3.0f, 0.001f);
    CheckVecNear(SampleLightmap(lm, 0, glm::vec3(3.5f, 0.0f, 3.5f)), 9.0f, 9.0f, 9.0f, 0.01f);
    // 中点 (g=0.75, 0.75)：lerp(lerp(0,1,.75), lerp(2,3,.75), .75) = 2.25
    CheckVecNear(SampleLightmap(lm, 0, glm::vec3(1.25f, 0.0f, 1.25f)), 2.25f, 2.25f, 2.25f, 0.05f);
    // 钳制：内容区外 → 边缘值
    CheckVecNear(SampleLightmap(lm, 0, glm::vec3(0.0f, 0.0f, 0.0f)), 0.0f, 0.0f, 0.0f, 0.001f);
    CheckVecNear(SampleLightmap(lm, 0, glm::vec3(9.0f, 0.0f, 9.0f)), 9.0f, 9.0f, 9.0f, 0.01f);
    // 越界 chart 索引 → 黑
    CheckVecNear(SampleLightmap(lm, 5, glm::vec3(1.5f, 0.0f, 1.5f)), 0.0f, 0.0f, 0.0f, 0.0f);
}

// 文本序列化往返：Save → Load 逐字节一致、采样值一致；损坏输入整体拒绝。
TEST_CASE("Lightmap.SerializeRoundTrip")
{
    std::vector<LightmapTri> tris;
    MakeFloor(tris, glm::vec3(0.8f, 0.6f, 0.4f));
    MakeWall(tris);
    LightmapBakeParams params;
    params.atlasSize = 64;
    params.worldTexelSize = 0.75f;
    params.skyColor = glm::vec3(0.3f, 0.4f, 0.5f);
    std::vector<DirectionalLightDesc> dirs;
    dirs.push_back(DirectionalLightDesc{glm::vec3(0.2f, -0.98f, 0.1f), glm::vec3(0.9f, 0.8f, 0.7f)});
    std::vector<PointLightDesc> points;
    LightmapResult lm;
    CHECK(BakeLightmap(tris, dirs, points, params, lm));

    const std::string path = "out/bh_lightmap_roundtrip.lm";
    Core::FileSystem::CreateDirs("out");
    CHECK(SaveLightmap(lm, path));
    LightmapResult loaded;
    CHECK(LoadLightmap(path, loaded));
    CHECK_EQ(loaded.atlasSize, lm.atlasSize);
    CHECK_NEAR(loaded.worldTexelSize, lm.worldTexelSize, 0.0f);
    CHECK_EQ(loaded.charts.size(), lm.charts.size());
    CHECK_EQ(loaded.texelsRgbe, lm.texelsRgbe);
    bool chartsEqual = true;
    for (size_t i = 0; i < lm.charts.size(); ++i)
    {
        const LightmapChart& a = lm.charts[i];
        const LightmapChart& b = loaded.charts[i];
        if (a.rectPos != b.rectPos || a.rectSize != b.rectSize || a.contentSize != b.contentSize ||
            a.axisU != b.axisU || a.axisV != b.axisV || a.originWorld != b.originWorld || a.texelSize != b.texelSize ||
            a.triIndex != b.triIndex)
            chartsEqual = false;
    }
    CHECK(chartsEqual);
    const glm::vec3 p(-4.0f, 0.0f, -4.5f);
    const glm::vec3 before = SampleLightmap(lm, FloorChart(p), p);
    const glm::vec3 after = SampleLightmap(loaded, FloorChart(p), p);
    CheckVecNear(before, after.x, after.y, after.z, 0.0f); // 逐字节一致 ⇒ 采样必然一致

    // 损坏输入整体拒绝：坏版本 / 缺 data / 未知字段 / 非法 hex / 数据长度不符 / 声明 chart 数不符
    std::vector<std::string> corrupted = {
        "garbage",
        "bighero-lightmap 2\nsize 4\n",
        "bighero-lightmap 1\nsize 4\ncharts 1\n",
        "bighero-lightmap 1\nsize 4\ncharts 1\nbogus 1\ndata\n" + std::string(4u * 4u * 4u * 2u, '0'),
        "bighero-lightmap 1\nsize 4\ncharts 1\ndata\n" + std::string(4u * 4u * 4u * 2u, 'X'),
        "bighero-lightmap 1\nsize 4\ncharts 1\ndata\n" + std::string(4u * 4u * 4u * 2u - 1u, '0'),
        "bighero-lightmap 1\nsize 4\ncharts 2\ndata\n" + std::string(4u * 4u * 4u * 2u, '0')};
    for (size_t i = 0; i < corrupted.size(); ++i)
    {
        const std::string bad = "out/bh_lightmap_bad" + std::to_string(i) + ".lm";
        Core::FileSystem::WriteText(bad, corrupted[i]);
        LightmapResult reject;
        CHECK(!LoadLightmap(bad, reject));
    }
    LightmapResult missing;
    CHECK(!LoadLightmap("out/bh_lightmap_no_such_file.lm", missing));
}

// 退化输入与参数拒绝。
TEST_CASE("Lightmap.Rejects")
{
    std::vector<DirectionalLightDesc> dirs;
    std::vector<PointLightDesc> points;
    LightmapBakeParams params;
    params.atlasSize = 256;
    params.worldTexelSize = 0.5f;
    LightmapResult lm;

    CHECK(!BakeLightmap({}, dirs, points, params, lm)); // 空几何
    std::vector<LightmapTri> degenerate(1);
    degenerate[0].a = degenerate[0].b = degenerate[0].c = glm::vec3(1.0f, 2.0f, 3.0f);
    CHECK(!BakeLightmap(degenerate, dirs, points, params, lm)); // 退化三角形

    std::vector<LightmapTri> tris;
    MakeFloor(tris);
    LightmapBakeParams badSize = params;
    badSize.atlasSize = 8; // < 16
    CHECK(!BakeLightmap(tris, dirs, points, badSize, lm));
    LightmapBakeParams badMargin = params;
    badMargin.margin = 0; // < 1
    CHECK(!BakeLightmap(tris, dirs, points, badMargin, lm));
    LightmapBakeParams badTexel = params;
    badTexel.worldTexelSize = 0.0f;
    CHECK(!BakeLightmap(tris, dirs, points, badTexel, lm));
}

// RGBE 量化：以最亮通道为基准的相对误差受控（共享指数的固有精度——暗通道
// 的绝对误差可达 max/512 量级，属格式契约）；近零钳制路径。
TEST_CASE("Lightmap.RgbeQuantization")
{
    const float vals[][3] = {{1.0f, 0.5f, 0.2f},
                             {0.256f, 0.128f, 0.064f},
                             {7.5f, 120.0f, 0.03f},
                             {0.0f, 0.0f, 0.0f},
                             {65536.0f, 1000.0f, 100.0f}};
    for (const float* v : vals)
    {
        uint8_t rgbe[4];
        const glm::vec3 src(glm::vec3(v[0], v[1], v[2]));
        RgbeEncode(src, rgbe);
        const glm::vec3 back = RgbeDecode(rgbe);
        const float srcMax = std::max(src.x, std::max(src.y, src.z));
        for (int k = 0; k < 3; ++k)
        {
            const float tol = std::max(0.02f, srcMax * 0.01f); // 相对最亮通道 1% 或绝对 0.02
            CHECK_NEAR(back[k], src[k], tol);
        }
    }
    uint8_t zero[4];
    RgbeEncode(glm::vec3(0.0f), zero);
    CHECK_EQ(zero[0] + zero[1] + zero[2] + zero[3], 0);
}

// 合并静态批次：顶点数/索引数/绕序保持 + UV 往返（顶点图集采样 == 该点烘焙值）+ 拒绝路径。
TEST_CASE("Lightmap.StaticBatch")
{
    std::vector<LightmapTri> tris;
    MakeFloor(tris, glm::vec3(1.0f));
    LightmapBakeParams params;
    params.atlasSize = 256;
    params.worldTexelSize = 0.5f;
    params.bakeSky = false;
    std::vector<DirectionalLightDesc> dirs;
    dirs.push_back(DirectionalLightDesc{glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.2f)});
    std::vector<PointLightDesc> points;
    LightmapResult lm;
    CHECK(BakeLightmap(tris, dirs, points, params, lm));

    LightmapStaticBatch batch;
    BuildStaticLightmapBatch(tris, lm, batch);
    CHECK_EQ(batch.vertices.size(), tris.size() * 3u);
    CHECK_EQ(batch.indices.size(), tris.size() * 3u);

    // UV 往返：每个顶点 lmUV 指向的 texel 解码值 == SampleLightmap(该顶点世界坐标)
    for (size_t i = 0; i < batch.vertices.size(); ++i)
    {
        const LightmapBatchVertex& v = batch.vertices[i];
        const int tx = static_cast<int>(v.lmUV.x * static_cast<float>(lm.atlasSize));
        const int ty = static_cast<int>(v.lmUV.y * static_cast<float>(lm.atlasSize));
        const size_t texel =
            (static_cast<size_t>(ty) * static_cast<size_t>(lm.atlasSize) + static_cast<size_t>(tx)) * 4u;
        const glm::vec3 sampled = RgbeDecode(&lm.texelsRgbe[texel]);
        const int tri = static_cast<int>(i / 3u);
        const glm::vec3 expected = SampleLightmap(lm, tri, v.pos);
        CHECK_NEAR(sampled.x, expected.x, 0.02f);
        CHECK_NEAR(sampled.y, expected.y, 0.02f);
        CHECK_NEAR(sampled.z, expected.z, 0.02f);
    }

    // 绕序保持：批次三角形法线 == 输入三角形法线（同向）
    for (size_t t = 0; t < tris.size(); ++t)
    {
        const glm::vec3 a = batch.vertices[t * 3u + 0u].pos;
        const glm::vec3 b = batch.vertices[t * 3u + 1u].pos;
        const glm::vec3 c = batch.vertices[t * 3u + 2u].pos;
        const glm::vec3 batchN = glm::normalize(glm::cross(b - a, c - a));
        const glm::vec3 triN = glm::normalize(glm::cross(tris[t].b - tris[t].a, tris[t].c - tris[t].a));
        CHECK_NEAR(glm::dot(batchN, triN), 1.0f, 1e-4f);
    }

    // 拒绝：三角形/chart 数失配 → 空批次
    std::vector<LightmapTri> mismatched(tris.begin(), tris.end() - 1);
    LightmapStaticBatch rejected;
    BuildStaticLightmapBatch(mismatched, lm, rejected);
    CHECK(rejected.vertices.empty() && rejected.indices.empty());
}

// BVH 等价性：随机场景 × 随机射线，BVH 任意命中与暴力遍历（任意命中语义）逐位一致。
// 覆盖 cullBackfaces 两种口径、selfIndex 跳过、minHitT 近距过滤、maxT 截断。
TEST_CASE("Lightmap.BvhEquivalence")
{
    auto lcg = [state = 123456789u]() mutable
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f; // [-1,1]
    };

    // 随机场景：地面 + 40 个随机大小/朝向的遮挡板（含 castShadow=false 混入）
    std::vector<LightmapTri> tris;
    LightmapTri floor;
    floor.a = glm::vec3(-10.0f, 0.0f, -10.0f);
    floor.b = glm::vec3(-10.0f, 0.0f, 10.0f);
    floor.c = glm::vec3(10.0f, 0.0f, 10.0f);
    tris.push_back(floor);
    for (int i = 0; i < 40; ++i)
    {
        const glm::vec3 c(lcg() * 8.0f, 1.0f + lcg() * 3.0f, lcg() * 8.0f);
        const float s = 0.3f + (lcg() * 0.5f + 0.5f) * 2.0f;
        LightmapTri t;
        t.a = c + glm::vec3(-s, lcg() * 0.5f, -s);
        t.b = c + glm::vec3(s, lcg() * 0.5f, -s * 0.5f);
        t.c = c + glm::vec3(lcg() * s, lcg() * 0.5f, s);
        t.castShadow = (i % 5 != 0); // 20% 不投影
        tris.push_back(t);
    }

    Render::detail::TriBvh bvh;
    bvh.Build(tris);

    const glm::dvec3 origin(0.5, 0.05, -0.5); // 地面附近
    long long agree = 0;
    long long total = 0;
    for (int i = 0; i < 2000; ++i)
    {
        // 随机方向（含向下/水平/向上的均匀混合）
        glm::dvec3 dir(lcg(), lcg(), lcg());
        const double len = glm::length(dir);
        if (len < 1e-4)
            continue;
        dir /= len;
        const double maxT = (i % 3 == 0) ? 3.0 : 1e30;
        const double minHitT = (i % 4 == 0) ? 0.05 : 0.005;
        const int selfIndex = (i % 7 == 0) ? static_cast<int>(i % tris.size()) : -1;
        for (const int cull : {1, 0})
        {
            LightmapBakeParams params;
            params.cullBackfaces = cull == 1;
            const bool brute =
                Render::detail::RayBlockedBruteForce(origin, dir, maxT, minHitT, tris, params, selfIndex);
            const bool fast = bvh.AnyHit(tris, origin, dir, maxT, minHitT, params.cullBackfaces, selfIndex);
            ++total;
            if (brute == fast)
                ++agree;
            else
                CHECK_EQ(brute, fast); // 失败时打印一次
        }
    }
    CHECK_EQ(total, 4000); // 2000 射线 × 2 种剔除口径
    CHECK_EQ(agree, total);
}
} // namespace