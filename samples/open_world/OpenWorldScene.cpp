// 开放世界场景构建（实现）：见 OpenWorldScene.h 的规格说明。
// 仅依赖 scene/Scene.h + glm + 标准 RNG（纯 CPU、确定性、可离线单测，不触碰 Vulkan/GPU）。
#include "open_world/OpenWorldScene.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <random>

namespace BigHero::Sample::OpenWorld
{
namespace
{
// ---- 世界常量 ----
constexpr float kWorldHalf = 160.0f;   // 半宽（世界 ±160 m）
constexpr float kChunkSize = 10.0f;    // 分块边长
constexpr int kChunkCount = 32;          // 每边块数（32 × 32 = 1024）
constexpr int kTotalChunks = 1024;
constexpr int kChunkCellsX = 32;
constexpr int kChunkCellsZ = 32;

// 密度层半径（距中心距离，米）与每块实体数
constexpr float kNearRadius = 40.0f;     // Near 层外缘
constexpr float kMidRadius = 80.0f;      // Mid 层外缘
constexpr float kFarRadius = 160.0f;     // Far 层外缘
constexpr int kNearDensity = 80;         // 每 Chunk 实体数
constexpr int kMidDensity = 40;
constexpr int kFarDensity = 15;
constexpr int kOuterDensity = 3;

// 动态实体比例（萤火虫 + 轻微浮动 = ≤5%）
constexpr float kDynamicRatio = 0.05f;

// 确定性 RNG 种子（纯函数契约：同 seed 同输出）
constexpr uint32_t kRngSeed = 42;

// 调色板（8 色，与 SliceScene 的 PBR 展示风格一致）
constexpr glm::vec3 kPalette[8] = {
    {0.55f, 0.27f, 0.07f}, // 深棕（树干/岩石）
    {0.33f, 0.55f, 0.20f}, // 深绿（针叶）
    {0.50f, 0.70f, 0.25f}, // 浅绿（阔叶）
    {0.65f, 0.55f, 0.35f}, // 沙褐（干草/灌木）
    {0.40f, 0.30f, 0.25f}, // 暗岩（巨石）
    {0.60f, 0.80f, 0.40f}, // 嫩绿（草丛）
    {0.72f, 0.52f, 0.04f}, // 金黄（枯草）
    {0.35f, 0.45f, 0.55f}, // 青灰（远岩）
};

// 萤火虫发光色（暖黄/橙红/淡绿）
constexpr glm::vec3 kFireflyColors[3] = {
    {1.0f, 0.95f, 0.40f},
    {1.0f, 0.60f, 0.15f},
    {0.60f, 1.0f, 0.45f},
};

// 局部伪随机：给 (chunkX, chunkZ, index) 一个确定性的 [0,1) 浮点数
// 基于简单线性同余（无需 full std::mt19937 的块级粒度）
inline float HashFloat(int cx, int cz, int idx, int salt)
{
    // LCG 参数（数值来源：Numerical Recipes）
    int64_t seed = static_cast<int64_t>(cx) * 73856093LL
                 + static_cast<int64_t>(cz) * 19349663LL
                 + static_cast<int64_t>(idx) * 83492791LL
                 + static_cast<int64_t>(salt) * 4294967291LL
                 + 1618033988LL; // 黄金比例常数
    seed = (seed * 1664525LL + 1013904223LL) & 0x7fffffffLL;
    return static_cast<float>(seed) / static_cast<float>(0x7fffffff);
}

// 组装场景物体（与 SliceScene 的 MakeProp 语义一致）
Scene::SceneObject MakeProp(const glm::vec3& position, float scale, const glm::vec3& tint,
                            float spinSpeed, float phase, uint32_t meshId,
                            float metallic, float roughness, int32_t parentIndex)
{
    Scene::SceneObject o;
    o.position = position;
    o.scale = scale;
    o.tint = tint;
    o.spinSpeed = spinSpeed;
    o.phase = phase;
    o.meshId = meshId;
    o.metallic = metallic;
    o.roughness = roughness;
    o.rotation = glm::vec3(0.0f);
    o.parentIndex = parentIndex;
    return o;
}

// 密度层判定：给定 Chunk 中心到原点的距离，返回密度
int DensityForChunk(float centerDist)
{
    if (centerDist < kNearRadius)
        return kNearDensity;
    if (centerDist < kMidRadius)
        return kMidDensity;
    if (centerDist < kFarRadius)
        return kFarDensity;
    return kOuterDensity;
}

// 在单个 Chunk 内生成实体。返回新增实体数。
// rng：每 Chunk 复用的 std::mt19937（seed 由全局种子 + chunk 坐标派生，保证确定性）
int PopulateChunk(std::vector<Scene::SceneObject>& objs, int cx, int cz,
                  int density, std::mt19937& rng)
{
    const float x0 = -kWorldHalf + static_cast<float>(cx) * kChunkSize;
    const float z0 = -kWorldHalf + static_cast<float>(cz) * kChunkSize;

    std::uniform_real_distribution<float> ux(x0, x0 + kChunkSize);
    std::uniform_real_distribution<float> uz(z0, z0 + kChunkSize);
    std::uniform_int_distribution<int> ukind(0, 7);     // 调色板
    std::uniform_int_distribution<int> uftype(0, 3);     // 4 种实体类型
    std::uniform_real_distribution<float> uscale(0.7f, 1.3f);
    std::uniform_real_distribution<float> urot(0.0f, 360.0f);
    std::uniform_real_distribution<float> uheight(0.0f, 0.3f); // 地面微起伏

    int added = 0;
    // 动态配额 = 块密度 × 5%，截断取整：密度 < 20 的块预算不足 1 只萤火虫时保持全静态，
    // 保证「动态 ≤5%」在全局统计上严格成立（外层更荒凉，符合叙事）。
    const int dynamicCount = static_cast<int>(static_cast<float>(density) * kDynamicRatio);
    const int staticCount = density - dynamicCount;

    // ---- 静态实体：岩石、矮树、草丛、灌木 ----
    for (int i = 0; i < staticCount; ++i)
    {
        const float px = ux(rng);
        const float pz = uz(rng);
        const float py = uheight(rng); // 轻微地面起伏
        const int paletteIdx = ukind(rng);
        const int ftype = uftype(rng);
        const float s = uscale(rng);
        const float phase = urot(rng);
        const glm::vec3 baseTint = kPalette[paletteIdx];

        // 类型 0=岩石(立方体, 金属度低, 粗糙高), 1=矮树(躯干立方体+球树冠, 链),
        //      2=草丛(小立方体), 3=灌木(胶囊体)
        if (ftype == 0)
        {
            // 岩石：立方体，随机倾斜（rotation.x/z 微调）
            auto o = MakeProp(glm::vec3(px, py + 0.3f * s, pz), 0.8f * s, baseTint * 0.7f,
                              0.0f, phase, 0u, 0.05f, 0.85f, -1);
            o.rotation = glm::vec3(HashFloat(cx, cz, i, 1) * 15.0f,
                                   HashFloat(cx, cz, i, 2) * 360.0f,
                                   HashFloat(cx, cz, i, 3) * 15.0f);
            objs.push_back(o);
        }
        else if (ftype == 1)
        {
            // 矮树：2 层链（躯干立方体 → 球树冠），父先于子入包
            const int parentIdx = static_cast<int>(objs.size());
            const float trunkH = 0.6f * s;
            const float trunkScale = 0.15f * s;
            objs.push_back(MakeProp(glm::vec3(px, py + trunkH * 0.5f, pz), trunkScale,
                                    baseTint * 0.6f, 0.0f, phase, 0u, 0.05f, 0.75f, -1));
            const float crownScale = 0.55f * s;
            objs.push_back(MakeProp(glm::vec3(0.0f, trunkH * 0.5f + crownScale * 0.4f, 0.0f), crownScale,
                                    baseTint * 1.1f, 0.0f, phase, 3u, 0.05f, 0.65f, parentIdx));
        }
        else if (ftype == 2)
        {
            // 草丛：小立方体，密集散布
            objs.push_back(MakeProp(glm::vec3(px, py + 0.08f * s, pz), 0.15f * s,
                                    baseTint * 1.15f, 0.0f, phase, 0u, 0.02f, 0.95f, -1));
        }
        else
        {
            // 灌木：胶囊体
            objs.push_back(MakeProp(glm::vec3(px, py + 0.25f * s, pz), 0.35f * s,
                                    baseTint * 0.9f, 0.0f, phase, 4u, 0.05f, 0.70f, -1));
        }
        ++added;
    }

    // ---- 动态实体：萤火虫（小球，轻微自转 + 上下浮动由 Update 驱动）----
    for (int i = 0; i < dynamicCount; ++i)
    {
        const float px = ux(rng);
        const float pz = uz(rng);
        const float py = 0.8f + 0.6f * HashFloat(cx, cz, staticCount + i, 4); // 悬浮高度 0.8~1.4m
        const int colorIdx = static_cast<int>(HashFloat(cx, cz, staticCount + i, 5) * 3.0f) % 3;
        const float s = 0.08f + 0.04f * HashFloat(cx, cz, staticCount + i, 6);
        const float speed = 30.0f + 60.0f * HashFloat(cx, cz, staticCount + i, 7);
        objs.push_back(MakeProp(glm::vec3(px, py, pz), s, kFireflyColors[colorIdx],
                                speed, urot(rng), 3u, 0.0f, 0.3f, -1));
        ++added;
    }

    return added;
}
} // namespace

std::vector<Scene::SceneObject> BuildOpenWorldScene()
{
    std::vector<Scene::SceneObject> objs;
    // 预分配：按 Near 层密度 × 总块数 = 最坏情况上限
    objs.reserve(static_cast<size_t>(kTotalChunks * kNearDensity));

    for (int cz = 0; cz < kChunkCellsZ; ++cz)
    {
        for (int cx = 0; cx < kChunkCellsX; ++cx)
        {
            const float centerX = -kWorldHalf + (static_cast<float>(cx) + 0.5f) * kChunkSize;
            const float centerZ = -kWorldHalf + (static_cast<float>(cz) + 0.5f) * kChunkSize;
            const float dist = std::sqrt(centerX * centerX + centerZ * centerZ);
            const int density = DensityForChunk(dist);
            if (density <= 0)
                continue;

            // 每 Chunk 独立 RNG：全局种子 + chunk 坐标混编，保证跨 chunk 独立且全局确定性
            const uint32_t chunkSeed = kRngSeed
                                       + static_cast<uint32_t>(cx) * 73856093u
                                       + static_cast<uint32_t>(cz) * 19349663u;
            std::mt19937 rng(chunkSeed);

            (void)PopulateChunk(objs, cx, cz, density, rng);
        }
    }

    return objs;
}

OpenWorldStats ComputeOpenWorldStats(const std::vector<Scene::SceneObject>& objs)
{
    OpenWorldStats st{};
    st.totalEntities = objs.size();
    if (st.totalEntities == 0)
        return st;

    // 动静分离
    for (const auto& o : objs)
    {
        if (o.spinSpeed == 0.0f)
            ++st.staticCount;
        else
            ++st.dynamicCount;
    }
    st.staticRatio = static_cast<float>(st.staticCount) / static_cast<float>(st.totalEntities);

    // 父子链统计（连续 parentIndex 链检测）
    st.minChainDepth = static_cast<size_t>(-1);
    st.maxChainDepth = 0;
    size_t i = 0;
    while (i < objs.size())
    {
        if (objs[i].parentIndex < 0)
        {
            // 根节点，探测链深度
            size_t depth = 1;
            size_t j = i + 1;
            while (j < objs.size() && objs[j].parentIndex >= static_cast<int32_t>(i)
                   && objs[j].parentIndex < static_cast<int32_t>(j))
            {
                ++depth;
                ++j;
            }
            if (depth > 1)
            {
                ++st.chainCount;
                st.minChainDepth = std::min(st.minChainDepth, depth);
                st.maxChainDepth = std::max(st.maxChainDepth, depth);
                // 动态子树：若链中有自转节点，整个链子树计入重算
                bool hasDynamic = false;
                for (size_t k = i; k < j; ++k)
                {
                    if (objs[k].spinSpeed != 0.0f)
                    {
                        hasDynamic = true;
                        break;
                    }
                }
                if (hasDynamic)
                    st.dynamicSubtreeNodeSum += depth;
            }
            i = j;
        }
        else
        {
            ++i;
        }
    }
    if (st.minChainDepth == static_cast<size_t>(-1))
        st.minChainDepth = 0;

    // 动态子树节点统计：对每个 spinSpeed != 0 的节点，统计其整棵子树大小。
    // 构建逻辑保证子节点连续且 parentIndex < 自身下标，故可用连续扫描。
    for (size_t di = 0; di < objs.size(); ++di)
    {
        if (objs[di].spinSpeed == 0.0f)
            continue;
        // 以 di 为根的子树大小（含自身及连续挂接的后代）
        size_t subtree = 1;
        size_t j = di + 1;
        while (j < objs.size() && objs[j].parentIndex >= static_cast<int32_t>(di)
               && objs[j].parentIndex < static_cast<int32_t>(j))
        {
            ++subtree;
            ++j;
        }
        st.dynamicSubtreeNodeSum += subtree;
    }

    // Chunk 密度层统计（复用构建逻辑）
    for (int cz = 0; cz < kChunkCellsZ; ++cz)
    {
        for (int cx = 0; cx < kChunkCellsX; ++cx)
        {
            const float centerX = -kWorldHalf + (static_cast<float>(cx) + 0.5f) * kChunkSize;
            const float centerZ = -kWorldHalf + (static_cast<float>(cz) + 0.5f) * kChunkSize;
            const float dist = std::sqrt(centerX * centerX + centerZ * centerZ);
            const int density = DensityForChunk(dist);
            if (density <= 0)
                continue;
            ++st.chunkCount;
            if (dist < kNearRadius)
                ++st.nearChunks;
            else if (dist < kMidRadius)
                ++st.midChunks;
            else if (dist < kFarRadius)
                ++st.farChunks;
            else
                ++st.outerChunks;
        }
    }

    return st;
}
} // namespace BigHero::Sample::OpenWorld