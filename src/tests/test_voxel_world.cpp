// 方块世界（VoxelWorld）单元测试：地形确定性 / 面剔除 / 顶点 AO / DDA 射线 /
// 碰撞体投影 / 编辑与流式加载。全部为纯 CPU 逻辑，离线可跑。
#include "framework/test_common.h"
#include "voxel/VoxelWorld.h"

#include <cmath>
#include <vector>

using namespace BigHero;
namespace Voxel = BigHero::Sample::Voxel;
using Voxel::BlockType;
using Voxel::VoxelConfig;
using Voxel::VoxelMesh;
using Voxel::VoxelWorld;

namespace
{
// 清空指定区块（用于构造受控的方块布局）
void ClearChunk(VoxelWorld& world, int cx, int cz)
{
    const VoxelConfig& cfg = world.Config();
    for (int y = 0; y < cfg.height; ++y)
    {
        for (int z = 0; z < cfg.chunkZ; ++z)
        {
            for (int x = 0; x < cfg.chunkX; ++x)
                world.Set(cx * cfg.chunkX + x, y, cz * cfg.chunkZ + z, BlockType::Air);
        }
    }
}
} // namespace

TEST_CASE("Voxel.DeterministicTerrain")
{
    const VoxelConfig cfg{};
    VoxelWorld a(cfg);
    VoxelWorld b(cfg);
    a.EnsureChunk(0, 0);
    b.EnsureChunk(0, 0);

    bool identical = true;
    for (int z = 0; z < cfg.chunkZ; ++z)
    {
        for (int x = 0; x < cfg.chunkX; ++x)
        {
            for (int y = 0; y < cfg.height; ++y)
            {
                if (a.Get(x, y, z) != b.Get(x, y, z))
                    identical = false;
            }
        }
    }
    CHECK(identical); // 同 seed 必须逐字节一致

    // 不同 seed 应产生不同地貌（否则说明噪声与种子无关）
    VoxelConfig other{};
    other.seed = cfg.seed + 987654u;
    VoxelWorld c(other);
    c.EnsureChunk(0, 0);
    // 不同 seed 必须产生显著不同的地貌：统计整区块差异格子数
    // （注意不能只比最高层——地形上限为 height-6，顶层恒为空气）
    int diffCount = 0;
    for (int y = 0; y < cfg.height; ++y)
    {
        for (int z = 0; z < cfg.chunkZ; ++z)
        {
            for (int x = 0; x < cfg.chunkX; ++x)
            {
                if (a.Get(x, y, z) != c.Get(x, y, z))
                    ++diffCount;
            }
        }
    }
    CHECK(diffCount > 100);

    // 未加载区块读取应安全返回 Air，不得隐式生成
    CHECK(a.Get(99999, 10, 99999) == BlockType::Air);
    CHECK(!a.HasChunk(77, 77));
}

TEST_CASE("Voxel.BlockAttributes")
{
    CHECK(!Voxel::IsBlockSolid(BlockType::Air));
    CHECK(!Voxel::IsBlockSolid(BlockType::Water));
    CHECK(Voxel::IsBlockSolid(BlockType::Stone));

    // 水为透明体：邻面剔除时不得遮挡（水下应可见）
    CHECK(!Voxel::IsBlockOpaque(BlockType::Water));
    CHECK(Voxel::IsBlockOpaque(BlockType::Grass));

    // 每种方块都应有有效（非零）反照率，否则渲染出来是纯黑
    const BlockType types[] = {BlockType::Grass,  BlockType::Dirt,  BlockType::Stone,
                               BlockType::Sand,   BlockType::Water, BlockType::Wood,
                               BlockType::Leaves, BlockType::Snow,  BlockType::Bedrock};
    for (BlockType t : types)
    {
        const glm::vec3 c = Voxel::BlockColor(t);
        CHECK(c.r > 0.0f && c.g > 0.0f && c.b > 0.0f);
    }
}

TEST_CASE("Voxel.FaceCulling")
{
    VoxelConfig cfg{};
    cfg.treeDensity = 0.0f; // 排除植被干扰，便于精确断言
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);
    ClearChunk(world, 0, 0);

    // 孤立方块：6 个面全部暴露
    world.Set(4, 30, 4, BlockType::Stone);
    VoxelMesh soloMesh;
    const auto solo = world.BuildChunkMeshWithStats(0, 0, soloMesh);
    CHECK(solo.faceCount == 6);
    CHECK(solo.vertexCount == 24);
    CHECK(solo.indexCount == 36);

    // 六面被实心包围后，中心方块不再产生任何面：
    // 7 个方块组成十字，中心 0 面 + 6 个外围各 5 面 = 30
    world.Set(5, 30, 4, BlockType::Stone);
    world.Set(3, 30, 4, BlockType::Stone);
    world.Set(4, 31, 4, BlockType::Stone);
    world.Set(4, 29, 4, BlockType::Stone);
    world.Set(4, 30, 5, BlockType::Stone);
    world.Set(4, 30, 3, BlockType::Stone);
    VoxelMesh crossMesh;
    const auto culled = world.BuildChunkMeshWithStats(0, 0, crossMesh);
    CHECK(culled.faceCount == 30);
}

TEST_CASE("Voxel.VertexAmbientOcclusion")
{
    VoxelConfig cfg{};
    cfg.treeDensity = 0.0f;
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);
    ClearChunk(world, 0, 0);

    // 开阔孤立方块：四周无遮挡，AO 应全为 1
    world.Set(4, 30, 4, BlockType::Stone);
    VoxelMesh openMesh;
    const auto open = world.BuildChunkMeshWithStats(0, 0, openMesh);
    CHECK(std::fabs(open.maxAo - 1.0f) < 1e-5f);
    CHECK(std::fabs(open.minAo - 1.0f) < 1e-5f);

    // 在其侧上方加一个方块，形成内凹：相邻顶点 AO 必须变暗（< 1）
    world.Set(5, 31, 4, BlockType::Stone);
    VoxelMesh occMesh;
    const auto occluded = world.BuildChunkMeshWithStats(0, 0, occMesh);
    CHECK(occluded.minAo < 1.0f);
    CHECK(occluded.minAo >= 0.5f); // AO 有下限，不得变全黑
}

TEST_CASE("Voxel.RaycastDda")
{
    VoxelConfig cfg{};
    cfg.treeDensity = 0.0f;
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);
    ClearChunk(world, 0, 0);

    // 沿 +Z 射向 (5,20,5)；命中面法线应为 -Z（射线从 -Z 侧进入）
    world.Set(5, 20, 5, BlockType::Stone);
    const auto hit = world.Raycast(glm::vec3(5.5f, 20.5f, 0.5f), glm::vec3(0.0f, 0.0f, 1.0f), 20.0f);
    CHECK(hit.hit);
    CHECK(hit.x == 5);
    CHECK(hit.y == 20);
    CHECK(hit.z == 5);
    CHECK(hit.block == BlockType::Stone);
    CHECK(std::fabs(hit.normal.z + 1.0f) < 1e-5f);
    CHECK(hit.distance > 4.0f && hit.distance < 5.0f); // 起点 z=0.5，方块近面 z=5

    // 射程不足时不命中
    const auto miss = world.Raycast(glm::vec3(5.5f, 20.5f, 0.5f), glm::vec3(0.0f, 0.0f, 1.0f), 2.0f);
    CHECK(!miss.hit);

    // 射向空气不命中
    const auto air = world.Raycast(glm::vec3(0.5f, 40.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
    CHECK(!air.hit);
}

TEST_CASE("Voxel.ColliderProjection")
{
    VoxelConfig cfg{};
    cfg.treeDensity = 0.0f;
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);
    ClearChunk(world, 0, 0);

    world.Set(5, 20, 5, BlockType::Stone);
    world.Set(5, 21, 5, BlockType::Stone);

    std::vector<Game::BoxCollider> colliders;
    world.CollectColliders(glm::vec3(5.5f, 21.0f, 5.5f), glm::vec3(0.35f, 0.9f, 0.35f), colliders);
    CHECK(colliders.size() == 2);

    // 每个碰撞体都应是「中心 +0.5、半尺寸 0.5」的单位方块
    for (const auto& c : colliders)
    {
        CHECK(std::fabs(c.half.x - 0.5f) < 1e-5f);
        CHECK(std::fabs(std::fmod(c.center.x, 1.0f) - 0.5f) < 1e-5f);
    }

    // 离远一点应收集不到（范围裁剪生效）
    std::vector<Game::BoxCollider> far;
    world.CollectColliders(glm::vec3(40.5f, 21.0f, 40.5f), glm::vec3(0.35f, 0.9f, 0.35f), far);
    CHECK(far.empty());
}

TEST_CASE("Voxel.Editing")
{
    VoxelConfig cfg{};
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);

    const int px = 8;
    const int pz = 8;
    int surface = -1;
    for (int y = cfg.height - 1; y >= 1; --y)
    {
        if (world.IsSolid(px, y, pz))
        {
            surface = y;
            break;
        }
    }
    CHECK(surface > 0); // 必须能找到地表

    // 破坏：地表方块置空后该处不再实心
    world.Set(px, surface, pz, BlockType::Air);
    CHECK(!world.IsSolid(px, surface, pz));
    CHECK(world.Get(px, surface, pz) == BlockType::Air);

    // 放置：在地表上方放一个方块
    world.Set(px, surface + 1, pz, BlockType::Wood);
    CHECK(world.Get(px, surface + 1, pz) == BlockType::Wood);

    // 越界编辑应被安全忽略（不崩溃、不改变世界状态）
    world.Set(px, cfg.height + 100, pz, BlockType::Stone);
    CHECK(world.Get(px, cfg.height + 100, pz) == BlockType::Air);
}

TEST_CASE("Voxel.Streaming")
{
    VoxelConfig cfg{};
    cfg.viewRadius = 2;
    VoxelWorld world(cfg);

    world.UpdateStreaming(glm::vec3(8.0f, 30.0f, 8.0f));
    const size_t loaded = world.ChunkCount();
    CHECK(loaded > 0);
    // 圆形视距 r=2：必然少于 5x5=25 且多于 1
    CHECK(loaded < 25);
    CHECK(world.HasChunk(0, 0));

    // 走到很远处：旧区块应被卸载，避免世界无限膨胀
    world.UpdateStreaming(glm::vec3(4000.0f, 30.0f, 4000.0f));
    CHECK(!world.HasChunk(0, 0));
    CHECK(world.ChunkCount() == loaded); // 数量稳定（同视距），而非单调增长
}

TEST_CASE("Voxel.SpawnPoint")
{
    VoxelConfig cfg{};
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);

    const glm::vec3 spawn = world.FindSpawn(8.0f, 8.0f);
    const int ix = static_cast<int>(std::floor(spawn.x));
    const int iz = static_cast<int>(std::floor(spawn.z));
    const int iy = static_cast<int>(std::floor(spawn.y));

    CHECK(spawn.y > 0.0f);
    CHECK(world.IsSolid(ix, iy - 1, iz));  // 脚下有支撑
    CHECK(!world.IsSolid(ix, iy, iz));     // 身体所在格为空
    CHECK(!world.IsSolid(ix, iy + 1, iz)); // 头顶格为空
}

TEST_CASE("Voxel.GreedyMeshing")
{
    VoxelConfig cfg{};
    cfg.treeDensity = 0.0f;
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);

    // ---- 1. 坐标系约定：方块 (x,y,z) 必须占据 [x,x+1]³，与 CollectColliders 完全一致 ----
    ClearChunk(world, 0, 0);
    world.Set(4, 30, 4, BlockType::Stone);
    VoxelMesh soloMesh;
    const auto solo = world.BuildChunkMeshWithStats(0, 0, soloMesh);
    CHECK(solo.faceCount == 6);
    CHECK(solo.vertexCount == 24);
    glm::vec3 lo(1.0e30f, 1.0e30f, 1.0e30f);
    glm::vec3 hi(-1.0e30f, -1.0e30f, -1.0e30f);
    for (const auto& v : soloMesh.vertices)
    {
        lo = glm::min(lo, v.pos);
        hi = glm::max(hi, v.pos);
    }
    CHECK(std::fabs(lo.x - 4.0f) < 1.0e-4f);
    CHECK(std::fabs(lo.y - 30.0f) < 1.0e-4f);
    CHECK(std::fabs(lo.z - 4.0f) < 1.0e-4f);
    CHECK(std::fabs(hi.x - 5.0f) < 1.0e-4f);
    CHECK(std::fabs(hi.y - 31.0f) < 1.0e-4f);
    CHECK(std::fabs(hi.z - 5.0f) < 1.0e-4f);

    // ---- 2. 整块实心：贪心合并后应只剩一个长方体的 6 个面 ----
    ClearChunk(world, 0, 0);
    for (int y = 0; y <= 30; ++y)
    {
        for (int z = 0; z < cfg.chunkZ; ++z)
        {
            for (int x = 0; x < cfg.chunkX; ++x)
                world.Set(x, y, z, BlockType::Stone);
        }
    }
    VoxelMesh solidMesh;
    const auto solid = world.BuildChunkMeshWithStats(0, 0, solidMesh);
    CHECK(solid.faceCount == 6);
    CHECK(solid.vertexCount == 24);
    CHECK(solid.indexCount == 36);
    // 朴素（逐面）实现为 2*16*16 + 4*16*31 = 2496 面，合并率必须 > 99%
    CHECK(solid.faceCount * 100 < 2496);
}

TEST_CASE("Voxel.GreedyMergeBoundaries")
{
    VoxelConfig cfg{};
    cfg.treeDensity = 0.0f;
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);

    // ---- 1. 不同类型不得跨类型合并（棋盘格：正交邻居必为异类）----
    ClearChunk(world, 0, 0);
    for (int y = 0; y <= 29; ++y)
    {
        for (int z = 0; z < cfg.chunkZ; ++z)
        {
            for (int x = 0; x < cfg.chunkX; ++x)
                world.Set(x, y, z, BlockType::Stone);
        }
    }
    for (int z = 0; z < cfg.chunkZ; ++z)
    {
        for (int x = 0; x < cfg.chunkX; ++x)
            world.Set(x, 30, z, ((x + z) % 2 == 0) ? BlockType::Stone : BlockType::Dirt);
    }
    VoxelMesh checkerMesh;
    const auto checker = world.BuildChunkMeshWithStats(0, 0, checkerMesh);
    CHECK(checker.faceCount >= 256); // 顶层 16*16 无法合并

    // ---- 2. AO 不一致时不得合并（障碍柱制造暗角，但合并仍须生效）----
    ClearChunk(world, 0, 0);
    for (int y = 0; y <= 30; ++y)
    {
        for (int z = 0; z < cfg.chunkZ; ++z)
        {
            for (int x = 0; x < cfg.chunkX; ++x)
                world.Set(x, y, z, BlockType::Stone);
        }
    }
    world.Set(5, 31, 5, BlockType::Stone); // 顶层之上的孤立方块 -> 周围顶面产生 AO
    VoxelMesh aoMesh;
    const auto ao = world.BuildChunkMeshWithStats(0, 0, aoMesh);
    CHECK(ao.faceCount > 6);   // AO 差异必须切开合并区域
    CHECK(ao.faceCount < 256); // 但平坦区域仍应大量合并
    CHECK(ao.minAo < 1.0f);    // 确实产生了 AO 暗角
    CHECK(ao.maxAo <= 1.0f + 1.0e-4f);
}

TEST_CASE("Voxel.GreedyTerrainGain")
{
    // 真实地形上的收益实测：把贪心结果和「逐面朴素实现」的理论面数对比
    VoxelConfig cfg{};
    VoxelWorld world(cfg);
    world.EnsureChunk(0, 0);

    VoxelMesh mesh;
    const auto stats = world.BuildChunkMeshWithStats(0, 0, mesh);

    uint32_t naive = 0;
    static const glm::ivec3 kNs[6] = {glm::ivec3(1, 0, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
                                      glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1),  glm::ivec3(0, 0, -1)};
    for (int y = 0; y < cfg.height; ++y)
    {
        for (int z = 0; z < cfg.chunkZ; ++z)
        {
            for (int x = 0; x < cfg.chunkX; ++x)
            {
                if (world.Get(x, y, z) == BlockType::Air)
                    continue;
                for (const glm::ivec3& n : kNs)
                {
                    if (!Voxel::IsBlockOpaque(world.Get(x + n.x, y + n.y, z + n.z)))
                        ++naive;
                }
            }
        }
    }

    CHECK(stats.faceCount <= naive);
    CHECK(stats.faceCount * 2 < naive); // 贪心至少砍掉一半面
    CHECK(stats.indexCount == stats.faceCount * 6);
    std::printf("      [greedy] chunk faces greedy=%u naive=%u ratio=%.1f%%\n", stats.faceCount, naive,
                naive > 0 ? (100.0f * static_cast<float>(stats.faceCount) / static_cast<float>(naive)) : 0.0f);
}
