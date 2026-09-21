#include "voxel/VoxelWorld.h"

#include <algorithm>
#include <array>

namespace BigHero::Sample::Voxel
{
namespace
{
// 顶点 AO 的最暗亮度（完全被三面遮挡时的反照率乘数）
constexpr float kAoMin = 0.55f;

// ---- 确定性整数 hash（无浮点累积误差，同 seed 同输入逐位一致）----
inline uint32_t HashU32(uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float Hash01(uint32_t x) noexcept
{
    return static_cast<float>(HashU32(x)) * (1.0f / 4294967295.0f);
}

inline float Hash2D(int x, int z, uint32_t seed) noexcept
{
    const uint32_t h =
        static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2654435761u;
    return Hash01(h);
}

inline float Hash3D(int x, int y, int z, uint32_t seed) noexcept
{
    const uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 1103515245u +
                       static_cast<uint32_t>(z) * 668265263u + seed * 2654435761u;
    return Hash01(h);
}

// 二维 value noise（smoothstep 插值，避免网格状伪影）
inline float ValueNoise2D(float x, float z, uint32_t seed) noexcept
{
    const int xi = static_cast<int>(std::floor(x));
    const int zi = static_cast<int>(std::floor(z));
    const float xf = x - static_cast<float>(xi);
    const float zf = z - static_cast<float>(zi);
    const float u = xf * xf * (3.0f - 2.0f * xf);
    const float v = zf * zf * (3.0f - 2.0f * zf);

    const float a = Hash2D(xi, zi, seed);
    const float b = Hash2D(xi + 1, zi, seed);
    const float c = Hash2D(xi, zi + 1, seed);
    const float d = Hash2D(xi + 1, zi + 1, seed);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

// 三维 value noise（洞穴挖空用）
inline float ValueNoise3D(float x, float y, float z, uint32_t seed) noexcept
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const int zi = static_cast<int>(std::floor(z));
    const float xf = x - static_cast<float>(xi);
    const float yf = y - static_cast<float>(yi);
    const float zf = z - static_cast<float>(zi);
    const float u = xf * xf * (3.0f - 2.0f * xf);
    const float v = yf * yf * (3.0f - 2.0f * yf);
    const float w = zf * zf * (3.0f - 2.0f * zf);

    const float c000 = Hash3D(xi, yi, zi, seed);
    const float c100 = Hash3D(xi + 1, yi, zi, seed);
    const float c010 = Hash3D(xi, yi + 1, zi, seed);
    const float c110 = Hash3D(xi + 1, yi + 1, zi, seed);
    const float c001 = Hash3D(xi, yi, zi + 1, seed);
    const float c101 = Hash3D(xi + 1, yi, zi + 1, seed);
    const float c011 = Hash3D(xi, yi + 1, zi + 1, seed);
    const float c111 = Hash3D(xi + 1, yi + 1, zi + 1, seed);

    const float x00 = c000 + (c100 - c000) * u;
    const float x10 = c010 + (c110 - c010) * u;
    const float x01 = c001 + (c101 - c001) * u;
    const float x11 = c011 + (c111 - c011) * u;
    const float y0 = x00 + (x10 - x00) * v;
    const float y1 = x01 + (x11 - x01) * v;
    return y0 + (y1 - y0) * w;
}

// 分形叠加（多八度，返回 [0,1]）
inline float Fbm2D(float x, float z, uint32_t seed, int octaves) noexcept
{
    float sum = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += ValueNoise2D(x * freq, z * freq, seed + static_cast<uint32_t>(i) * 1013u) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

// 立方体 6 面定义：法线 / 切线（UV +u）/ 4 角（局部 ±0.5，从外看逆时针）/ 面内两轴（AO 采样用）
struct FaceDef
{
    glm::vec3 normal;
    glm::vec3 tangent;
    std::array<glm::vec3, 4> corners;
    glm::vec3 axisU; // 面内第一轴
    glm::vec3 axisV; // 面内第二轴
};

const std::array<FaceDef, 6>& CubeFaces()
{
    static const std::array<FaceDef, 6> faces = {FaceDef{glm::vec3(0, 0, 1),
                                                         glm::vec3(1, 0, 0),
                                                         {glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0.5f, -0.5f, 0.5f),
                                                          glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(-0.5f, 0.5f, 0.5f)},
                                                         glm::vec3(1, 0, 0),
                                                         glm::vec3(0, 1, 0)},
                                                 FaceDef{glm::vec3(0, 0, -1),
                                                         glm::vec3(-1, 0, 0),
                                                         {glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(-0.5f, -0.5f, -0.5f),
                                                          glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0.5f, 0.5f, -0.5f)},
                                                         glm::vec3(1, 0, 0),
                                                         glm::vec3(0, 1, 0)},
                                                 FaceDef{glm::vec3(0, 1, 0),
                                                         glm::vec3(1, 0, 0),
                                                         {glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f),
                                                          glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(-0.5f, 0.5f, -0.5f)},
                                                         glm::vec3(1, 0, 0),
                                                         glm::vec3(0, 0, 1)},
                                                 FaceDef{glm::vec3(0, -1, 0),
                                                         glm::vec3(1, 0, 0),
                                                         {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, -0.5f, -0.5f),
                                                          glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(-0.5f, -0.5f, 0.5f)},
                                                         glm::vec3(1, 0, 0),
                                                         glm::vec3(0, 0, 1)},
                                                 FaceDef{glm::vec3(1, 0, 0),
                                                         glm::vec3(0, 0, -1),
                                                         {glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(0.5f, -0.5f, -0.5f),
                                                          glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f)},
                                                         glm::vec3(0, 0, 1),
                                                         glm::vec3(0, 1, 0)},
                                                 FaceDef{glm::vec3(-1, 0, 0),
                                                         glm::vec3(0, 0, 1),
                                                         {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(-0.5f, -0.5f, 0.5f),
                                                          glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(-0.5f, 0.5f, -0.5f)},
                                                         glm::vec3(0, 0, 1),
                                                         glm::vec3(0, 1, 0)}};
    return faces;
}

const std::array<glm::vec2, 4>& FaceUVs()
{
    static const std::array<glm::vec2, 4> uvs = {glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f),
                                                 glm::vec2(0.0f, 1.0f)};
    return uvs;
}
// ---- 贪心网格化（Greedy Meshing）----
// 切片平面上的一个「可合并单元」：方块类型 + 四角 AO。
// 只有类型与四角 AO 完全相同的相邻单元才会被合并成一个大四边形，
// 这样既保证 AO 正确（合并前后着色一致），又能把平坦表面的三角形数砍掉大半。
struct MaskCell
{
    bool visible = false;
    uint8_t block = 0;
    std::array<float, 4> ao{1.0f, 1.0f, 1.0f, 1.0f};
};

[[nodiscard]] inline bool SameMask(const MaskCell& a, const MaskCell& b) noexcept
{
    if (a.visible != b.visible || a.block != b.block)
        return false;
    for (int i = 0; i < 4; ++i)
    {
        if (std::fabs(a.ao[i] - b.ao[i]) > 1e-4f)
            return false;
    }
    return true;
}

[[nodiscard]] inline glm::ivec3 AxisUnit(int axis) noexcept
{
    return glm::ivec3(axis == 0 ? 1 : 0, axis == 1 ? 1 : 0, axis == 2 ? 1 : 0);
}

} // namespace

// ---------------------------------------------------------------- 方块属性

glm::vec3 BlockColor(BlockType type) noexcept
{
    switch (type)
    {
    case BlockType::Grass:
        return glm::vec3(0.29f, 0.52f, 0.20f);
    case BlockType::Dirt:
        return glm::vec3(0.45f, 0.32f, 0.20f);
    case BlockType::Stone:
        return glm::vec3(0.42f, 0.43f, 0.45f);
    case BlockType::Sand:
        return glm::vec3(0.85f, 0.79f, 0.58f);
    case BlockType::Water:
        return glm::vec3(0.16f, 0.36f, 0.62f);
    case BlockType::Wood:
        return glm::vec3(0.38f, 0.26f, 0.15f);
    case BlockType::Leaves:
        return glm::vec3(0.20f, 0.45f, 0.18f);
    case BlockType::Snow:
        return glm::vec3(0.92f, 0.94f, 0.97f);
    case BlockType::Bedrock:
        return glm::vec3(0.20f, 0.20f, 0.22f);
    case BlockType::Air:
    default:
        return glm::vec3(1.0f);
    }
}

bool IsBlockSolid(BlockType type) noexcept
{
    // 空气与水不阻挡移动（水为可涉水区域）
    return type != BlockType::Air && type != BlockType::Water;
}

bool IsBlockOpaque(BlockType type) noexcept
{
    // 仅空气与水透光：水为透明体，其相邻实心面仍需生成（水下可见）
    return type != BlockType::Air && type != BlockType::Water;
}

// ---------------------------------------------------------------- 构造与区块

VoxelWorld::VoxelWorld(const VoxelConfig& config) : config_(config)
{
    // 尺寸下限保护：过小的区块会让「树冠不跨界」的约束失效
    config_.chunkX = std::max(config_.chunkX, 8);
    config_.chunkZ = std::max(config_.chunkZ, 8);
    config_.height = std::clamp(config_.height, 16, 128);
    config_.seaLevel = std::clamp(config_.seaLevel, 1, config_.height - 4);
    config_.viewRadius = std::clamp(config_.viewRadius, 1, 12);
}

uint64_t VoxelWorld::ChunkKey(int cx, int cz) const noexcept
{
    // 低 32 位存 cx，高 32 位存 cz（坐标可能为负，故先转无符号再拼）
    return (static_cast<uint64_t>(static_cast<uint32_t>(cz)) << 32) | static_cast<uint32_t>(cx);
}

bool VoxelWorld::HasChunk(int cx, int cz) const noexcept
{
    return chunks_.find(ChunkKey(cx, cz)) != chunks_.end();
}

void VoxelWorld::EnsureChunk(int cx, int cz)
{
    const uint64_t key = ChunkKey(cx, cz);
    if (chunks_.find(key) != chunks_.end())
        return;
    Chunk chunk;
    GenerateChunk(cx, cz, chunk);
    chunks_.emplace(key, std::move(chunk));
}

void VoxelWorld::UnloadChunk(int cx, int cz)
{
    chunks_.erase(ChunkKey(cx, cz));
}

int VoxelWorld::HeightAt(int x, int z) const
{
    const uint32_t seed = config_.seed;
    const float rolling = Fbm2D(static_cast<float>(x) * 0.0125f, static_cast<float>(z) * 0.0125f, seed, 4);
    // 山脉：低频噪声平方后放大，形成稀疏高耸地形（而非全域抬升）
    const float mountain = Fbm2D(static_cast<float>(x) * 0.004f, static_cast<float>(z) * 0.004f, seed + 7777u, 3);
    const float h = rolling * 16.0f + mountain * mountain * 26.0f + 9.0f;
    const int hi = static_cast<int>(std::floor(h));
    return std::clamp(hi, 1, config_.height - 6);
}

void VoxelWorld::GenerateChunk(int cx, int cz, Chunk& chunk) const
{
    const int sx = config_.chunkX;
    const int sz = config_.chunkZ;
    const int h = config_.height;
    chunk.blocks.assign(static_cast<size_t>(sx) * sz * h, static_cast<uint8_t>(BlockType::Air));
    chunk.minY = h;
    chunk.maxY = 0;

    const uint32_t seed = config_.seed;
    auto setLocal = [&](int lx, int y, int lz, BlockType t)
    {
        if (lx < 0 || lx >= sx || lz < 0 || lz >= sz || y < 0 || y >= h)
            return;
        chunk.blocks[static_cast<size_t>((y * sz + lz) * sx + lx)] = static_cast<uint8_t>(t);
        chunk.minY = std::min(chunk.minY, y);
        chunk.maxY = std::max(chunk.maxY, y);
    };

    for (int lz = 0; lz < sz; ++lz)
    {
        for (int lx = 0; lx < sx; ++lx)
        {
            const int wx = cx * sx + lx;
            const int wz = cz * sz + lz;
            const int terrain = HeightAt(wx, wz);
            // 群系：低频温度噪声 —— 高温沙漠 / 低温雪原 / 中间草地
            const float temperature =
                Fbm2D(static_cast<float>(wx) * 0.003f, static_cast<float>(wz) * 0.003f, seed + 313u, 2);
            const bool desert = temperature > 0.66f;
            const bool snowy = temperature < 0.34f;

            for (int y = 0; y < h; ++y)
            {
                BlockType t = BlockType::Air;
                if (y == 0)
                {
                    t = BlockType::Bedrock; // 世界底层封底，防止挖穿掉出世界
                }
                else if (y <= terrain)
                {
                    // 洞穴：仅在地表层以下挖空，避免地表出现破洞
                    const bool cave = (y < terrain - 2) &&
                                      ValueNoise3D(static_cast<float>(wx) * 0.055f, static_cast<float>(y) * 0.09f,
                                                   static_cast<float>(wz) * 0.055f, seed + 555u) > 0.63f;
                    if (cave)
                    {
                        t = BlockType::Air;
                    }
                    else if (y == terrain)
                    {
                        if (terrain <= config_.seaLevel)
                            t = BlockType::Sand; // 水下/岸边统一沙地
                        else if (desert)
                            t = BlockType::Sand;
                        else if (snowy)
                            t = BlockType::Snow;
                        else
                            t = BlockType::Grass;
                    }
                    else if (y >= terrain - 3)
                    {
                        t = (terrain <= config_.seaLevel) ? BlockType::Sand : BlockType::Dirt;
                    }
                    else
                    {
                        t = BlockType::Stone;
                    }
                }
                else if (y <= config_.seaLevel)
                {
                    t = BlockType::Water;
                }

                if (t != BlockType::Air)
                    setLocal(lx, y, lz, t);
            }
        }
    }

    // ---- 植被：仅在草地群系的地表生成；树冠半径 2，故位置内缩 2 格保证不跨区块 ----
    for (int lz = 2; lz < sz - 2; ++lz)
    {
        for (int lx = 2; lx < sx - 2; ++lx)
        {
            const int wx = cx * sx + lx;
            const int wz = cz * sz + lz;
            const int terrain = HeightAt(wx, wz);
            if (terrain <= config_.seaLevel)
                continue; // 水下/滩涂不长树
            const float temperature =
                Fbm2D(static_cast<float>(wx) * 0.003f, static_cast<float>(wz) * 0.003f, seed + 313u, 2);
            if (temperature > 0.66f || temperature < 0.34f)
                continue; // 沙漠与雪原不长树
            if (Hash2D(wx, wz, seed + 99991u) >= config_.treeDensity)
                continue;

            const int trunkHeight = 4 + static_cast<int>(Hash2D(wx, wz, seed + 4242u) * 3.0f);
            for (int i = 1; i <= trunkHeight; ++i)
                setLocal(lx, terrain + i, lz, BlockType::Wood);

            const int crownY = terrain + trunkHeight;
            for (int dy = -1; dy <= 2; ++dy)
            {
                for (int dz = -2; dz <= 2; ++dz)
                {
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const float r2 = static_cast<float>(dx * dx) + static_cast<float>(dy * dy) * 1.6f +
                                         static_cast<float>(dz * dz);
                        if (r2 > 5.0f)
                            continue;
                        const int y = crownY + dy;
                        const int px = lx + dx;
                        const int pz = lz + dz;
                        if (px < 0 || px >= sx || pz < 0 || pz >= sz || y <= 0 || y >= h)
                            continue;
                        // 只填补空气，不覆盖已有方块（避免树叶吃掉树干）
                        const size_t idx = static_cast<size_t>((y * sz + pz) * sx + px);
                        if (chunk.blocks[idx] == static_cast<uint8_t>(BlockType::Air))
                            setLocal(px, y, pz, BlockType::Leaves);
                    }
                }
            }
        }
    }

    chunk.dirty = false;
    if (chunk.minY > chunk.maxY)
    {
        chunk.minY = 0;
        chunk.maxY = 0;
    }
}

void VoxelWorld::UpdateStreaming(const glm::vec3& position)
{
    const int pcx = ChunkCoordOf(position.x);
    const int pcz = ChunkCoordOf(position.z);
    const int r = config_.viewRadius;
    const float rLimit = static_cast<float>(r) + 0.5f;

    std::vector<uint64_t> keep;
    keep.reserve(static_cast<size_t>((2 * r + 1) * (2 * r + 1)));
    for (int dz = -r; dz <= r; ++dz)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            // 圆形视距（比方形更自然，也少加载约 21% 的角落区块）
            if (static_cast<float>(dx * dx + dz * dz) > rLimit * rLimit)
                continue;
            EnsureChunk(pcx + dx, pcz + dz);
            keep.push_back(ChunkKey(pcx + dx, pcz + dz));
        }
    }

    for (auto it = chunks_.begin(); it != chunks_.end();)
    {
        if (std::find(keep.begin(), keep.end(), it->first) == keep.end())
            it = chunks_.erase(it);
        else
            ++it;
    }
}

// ---------------------------------------------------------------- 方块存取

BlockType VoxelWorld::Get(int x, int y, int z) const noexcept
{
    if (y < 0 || y >= config_.height)
        return BlockType::Air;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / static_cast<float>(config_.chunkX)));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / static_cast<float>(config_.chunkZ)));
    const auto it = chunks_.find(ChunkKey(cx, cz));
    if (it == chunks_.end())
        return BlockType::Air;
    const int lx = x - cx * config_.chunkX;
    const int lz = z - cz * config_.chunkZ;
    const size_t idx = static_cast<size_t>((y * config_.chunkZ + lz) * config_.chunkX + lx);
    return static_cast<BlockType>(it->second.blocks[idx]);
}

void VoxelWorld::Set(int x, int y, int z, BlockType type)
{
    if (y < 0 || y >= config_.height)
        return;
    const int cx = static_cast<int>(std::floor(static_cast<float>(x) / static_cast<float>(config_.chunkX)));
    const int cz = static_cast<int>(std::floor(static_cast<float>(z) / static_cast<float>(config_.chunkZ)));
    const auto it = chunks_.find(ChunkKey(cx, cz));
    if (it == chunks_.end())
        return; // 未加载区块不支持编辑（避免隐式生成把世界撑爆）
    const int lx = x - cx * config_.chunkX;
    const int lz = z - cz * config_.chunkZ;
    const size_t idx = static_cast<size_t>((y * config_.chunkZ + lz) * config_.chunkX + lx);
    it->second.blocks[idx] = static_cast<uint8_t>(type);
    it->second.dirty = true;
    // 高度包围区间按需扩张（挖掉最高层时保守起见不做收缩，重算成本更高）
    if (type != BlockType::Air)
    {
        it->second.minY = std::min(it->second.minY, y);
        it->second.maxY = std::max(it->second.maxY, y);
    }
}

bool VoxelWorld::IsSolid(int x, int y, int z) const noexcept
{
    return IsBlockSolid(Get(x, y, z));
}

bool VoxelWorld::IsOpaque(int x, int y, int z) const noexcept
{
    return IsBlockOpaque(Get(x, y, z));
}

// ---------------------------------------------------------------- 网格生成

float VoxelWorld::VertexAo(bool side1, bool side2, bool corner) const noexcept
{
    if (side1 && side2)
        return kAoMin; // 两侧都被挡：最暗（拐角内凹）
    const int occ = (side1 ? 1 : 0) + (side2 ? 1 : 0) + (corner ? 1 : 0);
    const float t = (3.0f - static_cast<float>(occ)) / 3.0f;
    return kAoMin + (1.0f - kAoMin) * t;
}

VoxelMesh VoxelWorld::BuildChunkMesh(int cx, int cz) const
{
    VoxelMesh mesh;
    BuildChunkMeshWithStats(cx, cz, mesh);
    return mesh;
}

ChunkMeshStats VoxelWorld::BuildChunkMeshWithStats(int cx, int cz, VoxelMesh& outMesh) const
{
    ChunkMeshStats stats;
    outMesh.vertices.clear();
    outMesh.indices.clear();

    const auto it = chunks_.find(ChunkKey(cx, cz));
    if (it == chunks_.end())
        return stats;

    const Chunk& chunk = it->second;
    const int sx = config_.chunkX;
    const int sz = config_.chunkZ;
    const int sy = config_.height;
    const int baseX = cx * sx;
    const int baseZ = cz * sz;

    // 6 个面方向：法线 + 面内两轴（均取正轴向），满足 cross(vAxis, uAxis) == 法线。
    // 顶点绕序固定为 (-u,-v) -> (-u,+v) -> (+u,+v) -> (+u,-v)，右手定则下法线朝外。
    struct GreedyDir
    {
        glm::ivec3 normal;
        int uAxis; // 0=X 1=Y 2=Z
        int vAxis;
    };
    static const GreedyDir kDirs[] = {
        {glm::ivec3(0, 1, 0), 0, 2},  // +Y：u=X v=Z（cross(Z,X)=+Y）
        {glm::ivec3(0, -1, 0), 2, 0}, // -Y：u=Z v=X（cross(X,Z)=-Y）
        {glm::ivec3(1, 0, 0), 2, 1},  // +X：u=Z v=Y（cross(Y,Z)=+X）
        {glm::ivec3(-1, 0, 0), 1, 2}, // -X：u=Y v=Z（cross(Z,Y)=-X）
        {glm::ivec3(0, 0, 1), 1, 0},  // +Z：u=Y v=X（cross(X,Y)=+Z）
        {glm::ivec3(0, 0, -1), 0, 1}, // -Z：u=X v=Y（cross(Y,X)=-Z）
    };
    static const int kDu[4] = {-1, -1, 1, 1}; // 四顶点的 u 符号
    static const int kDv[4] = {-1, 1, 1, -1}; // 四顶点的 v 符号

    // 区块内直接索引（越界才回退到 Get），避免每格都查哈希
    auto blockAt = [&](int wx, int wy, int wz) -> BlockType
    {
        const int lx = wx - baseX;
        const int lz = wz - baseZ;
        if (lx >= 0 && lx < sx && lz >= 0 && lz < sz && wy >= 0 && wy < sy)
            return static_cast<BlockType>(chunk.blocks[static_cast<size_t>((wy * sz + lz) * sx + lx)]);
        return Get(wx, wy, wz);
    };
    auto opaqueAt = [&](int wx, int wy, int wz) { return IsBlockOpaque(blockAt(wx, wy, wz)); };

    const int dims[3] = {sx, sy, sz};
    const int base[3] = {baseX, 0, baseZ};

    outMesh.vertices.reserve(8192);
    outMesh.indices.reserve(12288);

    for (const GreedyDir& dir : kDirs)
    {
        const int wi = (dir.normal.x != 0) ? 0 : ((dir.normal.y != 0) ? 1 : 2);
        const int ui = dir.uAxis;
        const int vi = dir.vAxis;
        const int su = dims[ui];
        const int sv = dims[vi];
        const int sw = dims[wi];

        for (int w = 0; w < sw; ++w)
        {
            // ---- 1. 构建切片掩码：可见面 + 方块类型 + 四角 AO ----
            std::vector<MaskCell> mask(static_cast<size_t>(su) * sv);
            for (int v = 0; v < sv; ++v)
            {
                for (int u = 0; u < su; ++u)
                {
                    glm::ivec3 cell(0, 0, 0);
                    cell[ui] = base[ui] + u;
                    cell[vi] = base[vi] + v;
                    cell[wi] = base[wi] + w;

                    const BlockType self = blockAt(cell.x, cell.y, cell.z);
                    if (self == BlockType::Air)
                        continue;
                    const glm::ivec3 nb = cell + dir.normal;
                    const BlockType neighbor = blockAt(nb.x, nb.y, nb.z);
                    if (IsBlockOpaque(neighbor))
                        continue; // 被不透明方块遮挡
                    if (neighbor == BlockType::Water && self == BlockType::Water)
                        continue; // 水体内部不生成面

                    MaskCell mc;
                    mc.visible = true;
                    mc.block = static_cast<uint8_t>(self);
                    for (int c = 0; c < 4; ++c)
                    {
                        glm::ivec3 offU(0, 0, 0);
                        glm::ivec3 offV(0, 0, 0);
                        offU[ui] = kDu[c];
                        offV[vi] = kDv[c];
                        // AO 采样基准是「面外侧」那一格（cell + normal）
                        const bool side1 = opaqueAt(nb.x + offU.x, nb.y + offU.y, nb.z + offU.z);
                        const bool side2 = opaqueAt(nb.x + offV.x, nb.y + offV.y, nb.z + offV.z);
                        const bool cornerOcc =
                            opaqueAt(nb.x + offU.x + offV.x, nb.y + offU.y + offV.y, nb.z + offU.z + offV.z);
                        mc.ao[c] = VertexAo(side1, side2, cornerOcc);
                    }
                    mask[static_cast<size_t>(v) * su + u] = mc;
                }
            }

            // ---- 2. 贪心合并：先沿 u 扩宽，再沿 v 扩高 ----
            for (int v = 0; v < sv; ++v)
            {
                for (int u = 0; u < su; ++u)
                {
                    const MaskCell key = mask[static_cast<size_t>(v) * su + u];
                    if (!key.visible)
                        continue;

                    int wdt = 1;
                    while (u + wdt < su && SameMask(mask[static_cast<size_t>(v) * su + u + wdt], key))
                        ++wdt;

                    int hgt = 1;
                    bool expand = true;
                    while (expand && v + hgt < sv)
                    {
                        for (int k = 0; k < wdt; ++k)
                        {
                            if (!SameMask(mask[static_cast<size_t>(v + hgt) * su + u + k], key))
                            {
                                expand = false;
                                break;
                            }
                        }
                        if (expand)
                            ++hgt;
                    }

                    // 合并区域标记消费
                    for (int dv = 0; dv < hgt; ++dv)
                    {
                        for (int du = 0; du < wdt; ++du)
                            mask[static_cast<size_t>(v + dv) * su + u + du].visible = false;
                    }

                    // ---- 3. 输出四边形（世界坐标整数格点）----
                    glm::ivec3 origin(0, 0, 0);
                    origin[ui] = base[ui] + u;
                    origin[vi] = base[vi] + v;
                    // 面平面位于方块的哪一侧：法线为正取 +1，为负取方块自身边界
                    origin[wi] = base[wi] + w + (dir.normal[wi] > 0 ? 1 : 0);

                    glm::ivec3 stepU(0, 0, 0);
                    glm::ivec3 stepV(0, 0, 0);
                    stepU[ui] = wdt;
                    stepV[vi] = hgt;

                    const glm::ivec3 corners[4] = {origin, origin + stepV, origin + stepV + stepU, origin + stepU};
                    const glm::vec2 uvs[4] = {glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, static_cast<float>(hgt)),
                                              glm::vec2(static_cast<float>(wdt), static_cast<float>(hgt)),
                                              glm::vec2(static_cast<float>(wdt), 0.0f)};
                    const glm::vec3 baseColor = BlockColor(static_cast<BlockType>(key.block));
                    const glm::vec3 normal = glm::vec3(dir.normal.x, dir.normal.y, dir.normal.z);
                    const glm::vec3 tangent = glm::vec3(AxisUnit(ui));

                    // 水面略低于方块顶面（0.875 格高）：岸边形成自然落差，
                    // 水体不再像一块实心蓝砖（仅对水方块的 +Y 面生效）
                    const float wShift =
                        (key.block == static_cast<uint8_t>(BlockType::Water) && dir.normal.y > 0) ? -0.125f : 0.0f;

                    const uint32_t first = static_cast<uint32_t>(outMesh.vertices.size());
                    for (int c = 0; c < 4; ++c)
                    {
                        Scene::Vertex vt{};
                        glm::vec3 pos = glm::vec3(static_cast<float>(corners[c].x), static_cast<float>(corners[c].y),
                                                  static_cast<float>(corners[c].z));
                        pos[wi] += wShift;
                        vt.pos = pos;
                        vt.normal = normal;
                        vt.uv = uvs[c];
                        vt.color = baseColor * key.ao[c];
                        vt.tangent = tangent;
                        outMesh.vertices.push_back(vt);
                    }

                    // AO 对角翻转：让较暗的对角线成为三角形共享边，消除插值瑕疵
                    const bool flip = (key.ao[0] + key.ao[2]) < (key.ao[1] + key.ao[3]);
                    if (flip)
                    {
                        outMesh.indices.push_back(first + 1);
                        outMesh.indices.push_back(first + 2);
                        outMesh.indices.push_back(first + 3);
                        outMesh.indices.push_back(first + 1);
                        outMesh.indices.push_back(first + 3);
                        outMesh.indices.push_back(first + 0);
                    }
                    else
                    {
                        outMesh.indices.push_back(first + 0);
                        outMesh.indices.push_back(first + 1);
                        outMesh.indices.push_back(first + 2);
                        outMesh.indices.push_back(first + 0);
                        outMesh.indices.push_back(first + 2);
                        outMesh.indices.push_back(first + 3);
                    }

                    ++stats.faceCount;
                    for (int c = 0; c < 4; ++c)
                    {
                        stats.minAo = std::min(stats.minAo, key.ao[c]);
                        stats.maxAo = std::max(stats.maxAo, key.ao[c]);
                    }
                }
            }
        }
    }

    stats.vertexCount = static_cast<uint32_t>(outMesh.vertices.size());
    stats.indexCount = static_cast<uint32_t>(outMesh.indices.size());
    return stats;
}

// ---------------------------------------------------------------- 交互

VoxelHit VoxelWorld::Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance) const
{
    VoxelHit hit;
    const float len = glm::length(dir);
    if (len < 1e-6f)
        return hit;
    const glm::vec3 d = dir / len;

    // Amanatides & Woo 体素遍历：逐轴比较下一次跨越体素边界的参数 t，取最小者步进
    int ix = static_cast<int>(std::floor(origin.x));
    int iy = static_cast<int>(std::floor(origin.y));
    int iz = static_cast<int>(std::floor(origin.z));

    const int stepX = d.x > 0.0f ? 1 : -1;
    const int stepY = d.y > 0.0f ? 1 : -1;
    const int stepZ = d.z > 0.0f ? 1 : -1;

    // 轴分量为 0 时，tMax 取 +∞（该轴永不跨越），tDelta 同样取无穷
    constexpr float kInf = 1e30f;
    const float ax = std::fabs(d.x);
    const float ay = std::fabs(d.y);
    const float az = std::fabs(d.z);

    float tMaxX =
        ax < 1e-8f
            ? kInf
            : ((d.x > 0.0f ? (static_cast<float>(ix) + 1.0f - origin.x) : (origin.x - static_cast<float>(ix))) / ax);
    float tMaxY =
        ay < 1e-8f
            ? kInf
            : ((d.y > 0.0f ? (static_cast<float>(iy) + 1.0f - origin.y) : (origin.y - static_cast<float>(iy))) / ay);
    float tMaxZ =
        az < 1e-8f
            ? kInf
            : ((d.z > 0.0f ? (static_cast<float>(iz) + 1.0f - origin.z) : (origin.z - static_cast<float>(iz))) / az);
    const float tDeltaX = ax < 1e-8f ? kInf : 1.0f / ax;
    const float tDeltaY = ay < 1e-8f ? kInf : 1.0f / ay;
    const float tDeltaZ = az < 1e-8f ? kInf : 1.0f / az;

    float t = 0.0f;
    glm::vec3 normal(0.0f, -1.0f, 0.0f);

    // 起始体素若已实心（相机卡在方块内），直接判定命中
    if (IsSolid(ix, iy, iz))
    {
        hit.hit = true;
        hit.x = ix;
        hit.y = iy;
        hit.z = iz;
        hit.normal = normal;
        hit.distance = 0.0f;
        hit.block = Get(ix, iy, iz);
        return hit;
    }

    while (t <= maxDistance)
    {
        if (tMaxX < tMaxY && tMaxX < tMaxZ)
        {
            ix += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
            normal = glm::vec3(static_cast<float>(-stepX), 0.0f, 0.0f);
        }
        else if (tMaxY < tMaxZ)
        {
            iy += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
            normal = glm::vec3(0.0f, static_cast<float>(-stepY), 0.0f);
        }
        else
        {
            iz += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            normal = glm::vec3(0.0f, 0.0f, static_cast<float>(-stepZ));
        }

        if (t > maxDistance)
            break;
        if (iy < 0 || iy >= config_.height)
            continue;

        if (IsSolid(ix, iy, iz))
        {
            hit.hit = true;
            hit.x = ix;
            hit.y = iy;
            hit.z = iz;
            hit.normal = normal;
            hit.distance = t;
            hit.block = Get(ix, iy, iz);
            return hit;
        }
    }
    return hit;
}

void VoxelWorld::CollectColliders(const glm::vec3& center, const glm::vec3& half,
                                  std::vector<Game::BoxCollider>& out) const
{
    // 只收集玩家 AABB 外扩 1 格范围内的实心方块：典型 20~40 个，足够又不至于每帧刷爆
    const int x0 = static_cast<int>(std::floor(center.x - half.x)) - 1;
    const int x1 = static_cast<int>(std::floor(center.x + half.x)) + 1;
    const int y0 = static_cast<int>(std::floor(center.y - half.y)) - 1;
    const int y1 = static_cast<int>(std::floor(center.y + half.y)) + 1;
    const int z0 = static_cast<int>(std::floor(center.z - half.z)) - 1;
    const int z1 = static_cast<int>(std::floor(center.z + half.z)) + 1;

    out.clear();
    for (int y = y0; y <= y1; ++y)
    {
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                if (!IsSolid(x, y, z))
                    continue;
                // 方块 (x,y,z) 占据 [x,x+1)³，故中心为 +0.5、半尺寸 0.5
                out.push_back(Game::BoxCollider{
                    glm::vec3(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f),
                    glm::vec3(0.5f)});
            }
        }
    }
}

std::vector<VoxelWorld::ChunkCoord> VoxelWorld::LoadedChunks() const
{
    std::vector<ChunkCoord> out;
    out.reserve(chunks_.size());
    for (const auto& entry : chunks_)
    {
        // ChunkKey：低 32 位为 cx、高 32 位为 cz（按二进制位型还原，负数坐标亦正确）
        const int cx = static_cast<int>(static_cast<int32_t>(entry.first & 0xFFFFFFFFu));
        const int cz = static_cast<int>(static_cast<int32_t>((entry.first >> 32) & 0xFFFFFFFFu));
        out.push_back(ChunkCoord{cx, cz});
    }
    return out;
}

glm::vec3 VoxelWorld::FindSpawn(float x, float z) const
{
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    for (int y = config_.height - 1; y >= 1; --y)
    {
        if (IsSolid(ix, y, iz) && !IsSolid(ix, y + 1, iz) && !IsSolid(ix, y + 2, iz))
            return glm::vec3(static_cast<float>(ix) + 0.5f, static_cast<float>(y) + 1.05f,
                             static_cast<float>(iz) + 0.5f);
    }
    return glm::vec3(static_cast<float>(ix) + 0.5f, static_cast<float>(config_.seaLevel) + 2.0f,
                     static_cast<float>(iz) + 0.5f);
}
} // namespace BigHero::Sample::Voxel
