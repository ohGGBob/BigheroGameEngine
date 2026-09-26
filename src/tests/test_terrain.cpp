// 地形核心（scene/Terrain.h）：纯逻辑、零 GPU，可离线运行。
// 覆盖网格重建与拒绝 / 世界↔网格映射 / 双线性采样 / 线性斜坡解析法线 / 笔刷峰值与
// 单调衰减 / 平滑收缩 / 整平 / 分块网格与块边界缝合 / splat 层次混合规则 / 脏区域 /
// 文本序列化往返与损坏拒绝。
#include "framework/test_common.h"

#include "scene/Terrain.h"

using namespace BigHero;
using BigHero::Scene::BuildTerrainChunkMesh;
using BigHero::Scene::LoadTerrainHeightmap;
using BigHero::Scene::SaveTerrainHeightmap;
using BigHero::Scene::TerrainChunkMesh;
using BigHero::Scene::TerrainHeightmap;
using BigHero::Scene::TerrainSplatRule;
using BigHero::Scene::TerrainSplatWeights;

namespace
{
void CheckVecNear(const glm::vec3& v, const glm::vec3& e, float eps)
{
    CHECK_NEAR(v.x, e.x, eps);
    CHECK_NEAR(v.y, e.y, eps);
    CHECK_NEAR(v.z, e.z, eps);
}

// 网格参数拒绝 / 基本查询 / 世界↔网格映射 / 双线性 / 解析法线与坡度。
TEST_CASE("Terrain.GridSamplingAndNormals")
{
    TerrainHeightmap hm;
    CHECK(!hm.Resize(1, 4, 1.0f, glm::vec3(0.0f)));        // nx < 2
    CHECK(!hm.Resize(4, 4, 0.0f, glm::vec3(0.0f)));        // 非法 cell
    CHECK_EQ(hm.Nx(), 0);
    CHECK(hm.Resize(5, 4, 2.0f, glm::vec3(10.0f, 0.0f, -5.0f), 0.0f));
    CHECK_EQ(hm.Nx(), 5);
    CHECK_EQ(hm.Nz(), 4);
    CHECK_EQ(hm.CellSize(), 2.0f);
    CHECK_EQ(hm.Origin(), glm::vec3(10.0f, 0.0f, -5.0f));

    // 线性场 h = 3x + 2z（cell=2）
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 5; ++x)
            hm.SetHeight(x, z, 3.0f * static_cast<float>(x) + 2.0f * static_cast<float>(z));

    float gx = 0.0f;
    float gz = 0.0f;
    hm.WorldToGrid(glm::vec3(14.0f, 0.0f, -3.0f), gx, gz); // (14-10)/2=2, (-3+5)/2=1
    CHECK_NEAR(gx, 2.0f, 1e-5f);
    CHECK_NEAR(gz, 1.0f, 1e-5f);
    hm.WorldToGrid(glm::vec3(1000.0f, 0.0f, -1000.0f), gx, gz); // 越界钳制
    CHECK_NEAR(gx, 4.0f, 1e-5f);
    CHECK_NEAR(gz, 0.0f, 1e-5f);

    // 双线性（线性场中间点精确）
    const glm::vec3 wp(14.0f, 0.0f, -1.0f); // gx=2, gz=2
    CHECK_NEAR(hm.SampleWorld(wp), 3.0f * 2.0f + 2.0f * 2.0f, 1e-5f);
    const glm::vec3 wp2(13.0f, 0.0f, -2.0f); // gx=1.5, gz=1.5 → 4.5+3=7.5
    CHECK_NEAR(hm.SampleWorld(wp2), 7.5f, 1e-5f);

    // 解析法线：dh/dx = Δ(2格)/(2·2) = 6/4 = 1.5；dh/dz = 4/4 = 1 → normalize(-1.5, 1, -1)
    const glm::vec3 n = hm.NormalAt(2, 1);
    CheckVecNear(n, glm::normalize(glm::vec3(-1.5f, 1.0f, -1.0f)), 1e-5f);
    CHECK_NEAR(hm.SlopeAt(2, 1), std::sqrt(1.5f * 1.5f + 1.0f), 1e-5f);
    CHECK_NEAR(hm.MinHeight(), 0.0f, 1e-6f);
    CHECK_NEAR(hm.MaxHeight(), 3.0f * 4.0f + 2.0f * 3.0f, 1e-5f);
}

// 笔刷：峰值恰为 maxRise、沿半径单调衰减、半径外不变、反向压平。
TEST_CASE("Terrain.StampBrush")
{
    TerrainHeightmap hm;
    hm.Resize(21, 21, 1.0f, glm::vec3(0.0f), 0.0f);
    const glm::vec3 center(10.0f, 0.0f, 10.0f);
    hm.Stamp(center, 4.0f, 2.0f);

    CHECK_NEAR(hm.Height(10, 10), 2.0f, 1e-5f); // t=0 衰减 1
    CHECK_NEAR(hm.Height(12, 10), 2.0f * 0.5625f, 1e-4f); // t=0.5 → (1-0.25)²
    CHECK(hm.Height(10, 10) > hm.Height(11, 10) && hm.Height(11, 10) > hm.Height(13, 10)); // 单调
    CHECK_NEAR(hm.Height(15, 10), 0.0f, 1e-6f); // 半径外
    CHECK_NEAR(hm.Height(5, 3), 0.0f, 1e-6f);

    hm.ClearDirty();
    hm.Resize(21, 21, 1.0f, glm::vec3(0.0f), 0.0f); // 清零后压平：镜像对称
    hm.Stamp(center, 4.0f, -2.0f);
    CHECK_NEAR(hm.Height(10, 10), -2.0f, 1e-5f);
    CHECK_NEAR(hm.Height(12, 10), -2.0f * 0.5625f, 1e-4f);
}

// 平滑：单尖峰在 strength=1 下被邻域平均抹平、极值收缩；整平：区域内精确到目标值。
TEST_CASE("Terrain.SmoothAndFlatten")
{
    TerrainHeightmap hm;
    hm.Resize(21, 21, 1.0f, glm::vec3(0.0f), 0.0f);
    hm.SetHeight(10, 10, 8.0f);
    hm.Smooth(glm::vec3(10.0f, 0.0f, 10.0f), 2.0f, 1.0f); // 尖峰 + 4 邻域
    CHECK_NEAR(hm.Height(10, 10), 0.0f, 1e-5f); // 均值 = 0
    CHECK_NEAR(hm.Height(9, 10), 2.0f, 1e-5f);  // (8+0+0+0)/4
    CHECK(hm.MaxHeight() < 8.0f);               // 极值收缩
    CHECK_NEAR(hm.Height(5, 5), 0.0f, 1e-6f);   // 圈外不变

    // 整平：中心 (2,2) 半径 1.5 → 格 (1/2/3, 1/2/3) 内 d ≤ 1.5 者（含对角 √2）
    hm.FlattenTo(glm::vec3(2.0f, 0.0f, 2.0f), 1.5f, 7.0f);
    CHECK_NEAR(hm.Height(2, 2), 7.0f, 1e-5f);
    CHECK_NEAR(hm.Height(3, 3), 7.0f, 1e-5f); // d=√2 ≤ 1.5
    CHECK_NEAR(hm.Height(4, 2), 0.0f, 1e-6f); // 圈外
    CHECK_NEAR(hm.Height(0, 0), 0.0f, 1e-6f);
}

// 分块网格：顶点/索引计数、法线朝上、UV 平铺、块边界与邻块逐位一致、重复构建确定。
TEST_CASE("Terrain.ChunkMeshAndStitch")
{
    TerrainHeightmap hm;
    hm.Resize(10, 10, 1.0f, glm::vec3(0.0f), 5.0f);
    TerrainSplatRule rule;
    TerrainChunkMesh c0;
    BuildTerrainChunkMesh(hm, c0, 0, 0, 4, rule);
    CHECK_EQ(c0.positions.size(), 25u); // 5×5 顶点
    CHECK_EQ(c0.indices.size(), 4u * 4u * 6u);
    for (const glm::vec3& p : c0.positions)
        CHECK_NEAR(p.y, 5.0f, 1e-6f);
    for (const glm::vec3& nrm : c0.normals)
        CheckVecNear(nrm, glm::vec3(0.0f, 1.0f, 0.0f), 1e-5f);
    CHECK_NEAR(c0.uvs[0].x, 0.0f, 1e-6f);
    CHECK_NEAR(c0.uvs[24].x, 4.0f / 16.0f, 1e-6f);
    // 高度 5、坡度 0 → 草权重 1
    for (const glm::vec4& w : c0.splatWeights)
    {
        CHECK_NEAR(w.x, 1.0f, 1e-5f);
        CHECK_NEAR(w.y + w.z + w.w, 0.0f, 1e-5f);
    }

    // 邻块共享边界：x=4 列两块的顶点逐位一致
    TerrainChunkMesh c1;
    BuildTerrainChunkMesh(hm, c1, 1, 0, 4, rule);
    for (int z = 0; z < 4; ++z)
    {
        const glm::vec3 leftEdge = c0.positions[static_cast<size_t>(z) * 5u + 4u];
        const glm::vec3 rightEdge = c1.positions[static_cast<size_t>(z) * 5u + 0u];
        CheckVecNear(leftEdge, rightEdge, 1e-6f);
    }
    // 边界块截断：cz=2（z∈[8,9]，2 行）× 5 列 = 10 顶点
    TerrainChunkMesh c2;
    BuildTerrainChunkMesh(hm, c2, 0, 2, 4, rule);
    CHECK_EQ(c2.positions.size(), 10u);

    // 重复构建确定性
    TerrainChunkMesh c0b;
    BuildTerrainChunkMesh(hm, c0b, 0, 0, 4, rule);
    CHECK_EQ(c0b.positions, c0.positions);
    CHECK_EQ(c0b.indices, c0.indices);
}

// splat 层次混合：草（平缓中高）、岩（陡坡）、沙（低洼）、雪（高海拔）；权重和为 1。
TEST_CASE("Terrain.SplatRules")
{
    const TerrainSplatRule rule{};
    // 草：平缓、中高
    CHECK_NEAR(TerrainSplatWeights(2.0f, 0.0f, rule).x, 1.0f, 1e-5f);
    // 岩：陡坡
    CHECK_NEAR(TerrainSplatWeights(2.0f, 3.0f, rule).y, 1.0f, 1e-5f);
    // 沙：低洼
    CHECK_NEAR(TerrainSplatWeights(0.0f, 0.0f, rule).z, 1.0f, 1e-5f);
    // 雪：高海拔
    CHECK_NEAR(TerrainSplatWeights(10.0f, 0.0f, rule).w, 1.0f, 1e-5f);
    // 过渡与守恒：任意采样权和 == 1 且非负
    const float hs[] = {-1.0f, 0.1f, 0.8f, 2.0f, 6.0f, 7.5f, 9.5f};
    const float ss[] = {0.0f, 0.4f, 0.9f, 1.5f, 4.0f};
    for (const float h : hs)
    {
        for (const float s : ss)
        {
            const glm::vec4 w = TerrainSplatWeights(h, s, rule);
            CHECK_NEAR(w.x + w.y + w.z + w.w, 1.0f, 1e-5f);
            CHECK(w.x >= 0.0f && w.y >= 0.0f && w.z >= 0.0f && w.w >= 0.0f);
        }
    }
}

// 脏区域：编辑只染涉及块；ClearDirty 复位。
TEST_CASE("Terrain.DirtyRegion")
{
    TerrainHeightmap hm;
    hm.Resize(32, 32, 1.0f, glm::vec3(0.0f), 0.0f);
    hm.ClearDirty();
    constexpr int kChunk = 8;
    CHECK(!hm.ChunkDirty(0, 0, kChunk));
    hm.Stamp(glm::vec3(16.0f, 0.0f, 16.0f), 4.0f, 1.0f); // 触及块 (1,1)/(2,1)/(1,2)/(2,2)
    CHECK(hm.ChunkDirty(1, 1, kChunk) || hm.ChunkDirty(2, 1, kChunk) || hm.ChunkDirty(1, 2, kChunk) ||
          hm.ChunkDirty(2, 2, kChunk));
    CHECK(!hm.ChunkDirty(0, 3, kChunk)); // 远块不受影响
    hm.ClearDirty();
    CHECK(!hm.ChunkDirty(2, 2, kChunk));
}

// 文本序列化：往返逐位一致；损坏/截断/未知字段整体拒绝。
TEST_CASE("Terrain.Serialization")
{
    TerrainHeightmap hm;
    hm.Resize(9, 7, 0.5f, glm::vec3(-2.0f, 0.0f, 3.0f), 0.0f);
    hm.Stamp(glm::vec3(0.0f, 0.0f, 0.0f), 2.0f, 1.5f);
    for (int x = 0; x < 9; ++x)
        hm.SetHeight(x, 3, static_cast<float>(x) * 0.25f);

    Core::FileSystem::CreateDirs("out");
    const std::string path = "out/bh_terrain_roundtrip.terrain";
    CHECK(SaveTerrainHeightmap(hm, path));
    TerrainHeightmap loaded;
    CHECK(LoadTerrainHeightmap(path, loaded));
    CHECK_EQ(loaded.Nx(), hm.Nx());
    CHECK_EQ(loaded.Nz(), hm.Nz());
    CHECK_EQ(loaded.CellSize(), hm.CellSize());
    CHECK_EQ(loaded.Origin(), hm.Origin());
    bool allEqual = true;
    for (int z = 0; z < hm.Nz() && allEqual; ++z)
        for (int x = 0; x < hm.Nx(); ++x)
            if (loaded.Height(x, z) != hm.Height(x, z)) // %.9g 十进制往返保真
                allEqual = false;
    CHECK(allEqual);
    CHECK_NEAR(loaded.SampleWorld(glm::vec3(0.5f, 0.0f, -1.0f)), hm.SampleWorld(glm::vec3(0.5f, 0.0f, -1.0f)), 0.0f);

    // 损坏/截断/非法拒绝
    std::vector<std::string> corrupted = {
        "garbage",
        "bighero-terrain 2\ngrid 4 4\ncell 1\norigin 0 0 0\ndata\n0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n",
        "bighero-terrain 1\ngrid 1 4\ncell 1\norigin 0 0 0\ndata\n0 0 0 0\n",
        "bighero-terrain 1\ngrid 4 4\ncell 1\norigin 0 0 0\nbogus 1\n",
        "bighero-terrain 1\ngrid 4 4\ncell 1\norigin 0 0 0\ndata\n0 0 0\n", // 数据不足
        "bighero-terrain 1\ncell 1\norigin 0 0 0\ndata\n0 0 0 0\n"};
    for (size_t i = 0; i < corrupted.size(); ++i)
    {
        const std::string bad = "out/bh_terrain_bad" + std::to_string(i) + ".terrain";
        Core::FileSystem::WriteText(bad, corrupted[i]);
        TerrainHeightmap reject;
        CHECK(!LoadTerrainHeightmap(bad, reject));
    }
    TerrainHeightmap missing;
    CHECK(!LoadTerrainHeightmap("out/bh_terrain_no_such.terrain", missing));
}
} // namespace