#pragma once
// 方块世界（Voxel World）：Minecraft 式可交互地形样本。
//
// 与 OpenWorldScene（「实体云」式大规模压测）互补：本模块是**真实可玩**的体素世界——
// 区块化按需生成的无限地形、确定性噪声地貌（含生物群系/水域/洞穴/植被）、
// 面剔除 + 顶点级 AO 的网格合并、DDA 体素射线（破坏/放置）、AABB 碰撞体投影
// （直接喂给 Game::FpController 做陆行碰撞）。
//
// 设计约束（与项目既有样本一致）：
//   - 纯 CPU、零 Vulkan/零窗口依赖，可离线单测、CI 可回归；
//   - 确定性：同 seed 同输入逐字节一致（RNG 为整数 hash，无浮点累积误差）；
//   - 网格顶点直接输出世界坐标，渲染侧只需 identity 实例即可绘制（配合顶点色 AO）。

#include "game/FpController.h" // Game::BoxCollider（碰撞体投影目标结构）
#include "scene/CubeMesh.h"    // Scene::Vertex（顶点格式：pos/normal/uv/color/tangent）

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace BigHero::Sample::Voxel
{
// 方块类型。Air/Water 非实心（不产生碰撞、不遮挡邻面），其余均为实心不透明。
enum class BlockType : uint8_t
{
    Air = 0,
    Grass,  // 草地
    Dirt,   // 泥土
    Stone,  // 岩石
    Sand,   // 沙
    Water,  // 水（半透明，非实心）
    Wood,   // 树干
    Leaves, // 树叶
    Snow,   // 雪
    Bedrock // 基岩（世界底层，不可挖穿）
};

struct VoxelConfig
{
    int chunkX = 16;           // 区块 X 边长（格）
    int chunkZ = 16;           // 区块 Z 边长（格）
    int height = 64;           // 世界高度上限（y ∈ [0, height)）
    int seaLevel = 20;         // 海平面（低于此且地形未及则填水）
    uint32_t seed = 20260921;  // 地形种子（确定性）
    int viewRadius = 4;        // 流式加载视距（区块数，含自身）
    float treeDensity = 0.02f; // 每格成树概率（草地群系）
};

// 方块反照率（线性空间基色，再乘 AO 亮度得到顶点色）
[[nodiscard]] glm::vec3 BlockColor(BlockType type) noexcept;
// 是否实心（参与碰撞与遮挡判定）
[[nodiscard]] bool IsBlockSolid(BlockType type) noexcept;
// 是否不透明（邻面剔除依据；水为透明，故水下方块的侧面仍需生成）
[[nodiscard]] bool IsBlockOpaque(BlockType type) noexcept;

// 体素射线命中结果（DDA 遍历，返回首个实心方块及其入射面）
struct VoxelHit
{
    bool hit = false; // 是否命中
    int x = 0;        // 命中方块坐标
    int y = 0;
    int z = 0;
    glm::vec3 normal{0.0f}; // 入射面外法线（单位向量；放置方块时沿此方向偏移）
    float distance = 0.0f;  // 命中距离（米）
    BlockType block = BlockType::Air;
};

// 区块网格（合并后的世界坐标几何，供渲染侧上传为一个 Mesh）
struct VoxelMesh
{
    std::vector<Scene::Vertex> vertices;
    std::vector<uint32_t> indices;
};

// 单区块统计（单测用：验证面剔除与 AO 的正确性）
struct ChunkMeshStats
{
    uint32_t faceCount = 0; // 生成的面数（每面 4 顶点 + 6 索引）
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    float minAo = 1.0f; // 顶点 AO 最小值（应 ∈ [kAoMin, 1]）
    float maxAo = 0.0f;
};

class VoxelWorld
{
  public:
    explicit VoxelWorld(const VoxelConfig& config = VoxelConfig{});

    // ---- 区块生命周期 ----
    // 按需生成区块（确定性；已存在则直接返回）
    void EnsureChunk(int cx, int cz);
    [[nodiscard]] bool HasChunk(int cx, int cz) const noexcept;
    void UnloadChunk(int cx, int cz);
    [[nodiscard]] size_t ChunkCount() const noexcept { return chunks_.size(); }

    // 流式：以世界坐标为中心，加载视距内区块、卸载视距外区块
    void UpdateStreaming(const glm::vec3& position);
    // 已加载区块坐标列表（供渲染侧枚举并生成网格；迭代顺序不保证稳定）
    struct ChunkCoord
    {
        int x = 0;
        int z = 0;
    };
    [[nodiscard]] std::vector<ChunkCoord> LoadedChunks() const;
    // 世界坐标 -> 区块坐标（向下取整，负数也正确）
    [[nodiscard]] int ChunkCoordOf(float worldValue) const noexcept
    {
        return static_cast<int>(std::floor(worldValue / static_cast<float>(config_.chunkX)));
    }

    // ---- 方块存取 ----
    // 越界 / 未加载区块一律返回 Air（不触发隐式生成，避免查询把世界撑爆）
    [[nodiscard]] BlockType Get(int x, int y, int z) const noexcept;
    // 编辑方块：越界或未加载区块则忽略；成功后标记所在区块为脏
    void Set(int x, int y, int z, BlockType type);
    [[nodiscard]] bool IsSolid(int x, int y, int z) const noexcept;
    [[nodiscard]] bool IsOpaque(int x, int y, int z) const noexcept;

    // ---- 网格 ----
    // 生成区块合并网格：仅输出「朝向空气/透明体的暴露面」，顶点色含 AO
    [[nodiscard]] VoxelMesh BuildChunkMesh(int cx, int cz) const;
    [[nodiscard]] ChunkMeshStats BuildChunkMeshWithStats(int cx, int cz, VoxelMesh& outMesh) const;

    // ---- 交互 ----
    // DDA 体素遍历：从 origin 沿 dir 找首个实心方块（maxDistance 米内）
    [[nodiscard]] VoxelHit Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance = 6.0f) const;
    // 收集与给定 AABB 范围相交的实心方块碰撞体（喂给 FpController）
    void CollectColliders(const glm::vec3& center, const glm::vec3& half, std::vector<Game::BoxCollider>& out) const;
    // 地表出生点：指定水平位置处最高实心方块之上（+1.05m 避免卡进方块）
    [[nodiscard]] glm::vec3 FindSpawn(float x, float z) const;

    [[nodiscard]] const VoxelConfig& Config() const noexcept { return config_; }

    // 运行时调整视距（区块数）：收放加载半径，下一次 UpdateStreaming 生效。
    // 返回是否真的变化（用于决定是否需要立刻重新流式加载）。
    bool SetViewRadius(int radius) noexcept
    {
        const int clamped = (radius < 1) ? 1 : ((radius > 12) ? 12 : radius);
        if (clamped == config_.viewRadius)
            return false;
        config_.viewRadius = clamped;
        return true;
    }

  private:
    struct Chunk
    {
        std::vector<uint8_t> blocks; // 扁平存储：index = (y * cz + z) * cx + x
        int minY = 0;                // 该区块最低非空气层（网格生成下界，跳过全空区间）
        int maxY = 0;                // 该区块最高非空气层
        bool dirty = true;
    };

    [[nodiscard]] uint64_t ChunkKey(int cx, int cz) const noexcept;
    void GenerateChunk(int cx, int cz, Chunk& chunk) const;
    [[nodiscard]] int HeightAt(int x, int z) const; // 确定性地形高度（含群系/水域前的裸高）
    // 顶点 AO：由面外侧的 side1/side2/corner 三个邻居实心情况推导（经典体素 AO）
    [[nodiscard]] float VertexAo(bool side1, bool side2, bool corner) const noexcept;

    VoxelConfig config_;
    std::unordered_map<uint64_t, Chunk> chunks_;
};
} // namespace BigHero::Sample::Voxel
