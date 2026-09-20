// 赛博朋克城市开放世界场景（实现）：见 OpenWorldScene.h 规格。
// 基于 10×10 m 分块网格 + 曼哈顿式道路网（每 5 块一条主干道）+ 距离分层建筑密度。
// 仅依赖 scene/Scene.h + glm + 标准 RNG（纯 CPU、确定性、可离线单测）。
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
constexpr float kWorldHalf = 160.0f; // 半宽（±160 m）
constexpr float kChunkSize = 10.0f;  // 分块边长
constexpr int   kChunkCells = 32;    // 每边 32 块（32×32 = 1024）
constexpr int   kTotalChunks = 1024;

// 密度层半径（距中心距离，米）
constexpr float kNearRadius = 40.0f;
constexpr float kMidRadius  = 80.0f;
constexpr float kFarRadius  = 160.0f;

// 每 Chunk 实体数（旧版保留，用于 chunk 内建筑/道路细节密度）
constexpr int kNearDensity = 80;
constexpr int kMidDensity  = 40;
constexpr int kFarDensity  = 15;
constexpr int kOuterDensity = 3;

// 动态实体比例（霓虹闪烁 + 雨水 + 全息投影 = ≤3%，全局硬性上限）
constexpr float kDynamicRatio = 0.03f;

// 确定性 RNG 种子
constexpr uint32_t kRngSeed = 42;

// ---- 赛博朋克城市调色板（16 色） ----
constexpr glm::vec3 kPalette[16] = {
    {0.10f, 0.10f, 0.12f}, // 0: 沥青深灰（道路基座）
    {0.70f, 0.72f, 0.75f}, // 1: 金属银
    {0.30f, 0.50f, 0.60f}, // 2: 玻璃蓝
    {1.00f, 0.10f, 0.20f}, // 3: 霓虹红
    {0.10f, 0.30f, 1.00f}, // 4: 霓虹蓝
    {0.60f, 0.10f, 0.90f}, // 5: 霓虹紫
    {0.95f, 0.85f, 0.10f}, // 6: 霓虹黄
    {0.50f, 0.20f, 0.05f}, // 7: 锈铁
    {0.40f, 0.40f, 0.42f}, // 8: 混凝土灰（人行道/建筑基座）
    {0.80f, 0.20f, 0.20f}, // 9: 人行道红
    {0.90f, 0.90f, 0.90f}, // 10: 人行横道白
    {0.05f, 0.05f, 0.08f}, // 11: 夜雾黑
    {1.00f, 0.95f, 0.80f}, // 12: 发光暖白（路灯/灯球）
    {0.20f, 0.60f, 1.00f}, // 13: 全息蓝
    {0.80f, 0.30f, 1.00f}, // 14: 全息紫
    {0.20f, 1.00f, 0.40f}, // 15: 全息绿
};

// 路灯/霓虹发光色
constexpr glm::vec3 kLightColors[4] = {
    {1.0f, 0.95f, 0.60f}, // 暖白
    {1.0f, 0.20f, 0.30f}, // 霓虹红
    {0.20f, 0.60f, 1.0f}, // 霓虹蓝
    {0.70f, 0.20f, 1.0f}, // 霓虹紫
};

// ---- 局部伪随机（LCG） ----
inline float HashFloat(int cx, int cz, int idx, int salt)
{
    int64_t seed = static_cast<int64_t>(cx) * 73856093LL
                 + static_cast<int64_t>(cz) * 19349663LL
                 + static_cast<int64_t>(idx) * 83492791LL
                 + static_cast<int64_t>(salt) * 4294967291LL
                 + 1618033988LL;
    seed = (seed * 1664525LL + 1013904223LL) & 0x7fffffffLL;
    return static_cast<float>(seed) / static_cast<float>(0x7fffffff);
}

// ---- 组装场景物体 ----
Scene::SceneObject MakeProp(const glm::vec3& position, float scale, const glm::vec3& tint,
                            float spinSpeed, float phase, uint32_t meshId,
                            float metallic, float roughness, int32_t parentIndex)
{
    Scene::SceneObject o;
    o.position    = position;
    o.scale       = scale;
    o.tint        = tint;
    o.spinSpeed   = spinSpeed;
    o.phase       = phase;
    o.meshId      = meshId;
    o.metallic    = metallic;
    o.roughness   = roughness;
    o.rotation    = glm::vec3(0.0f);
    o.parentIndex = parentIndex;
    return o;
}

// 密度层判定
int DensityForChunk(float centerDist)
{
    if (centerDist < kNearRadius) return kNearDensity;
    if (centerDist < kMidRadius)  return kMidDensity;
    if (centerDist < kFarRadius)  return kFarDensity;
    return kOuterDensity;
}

// 道路判定：曼哈顿网格主干道（每 5 个 chunk 一条）
inline bool IsRoadChunk(int cx, int cz) { return (cx % 5 == 0) || (cz % 5 == 0); }
inline bool IsCrossroad(int cx, int cz) { return (cx % 5 == 0) && (cz % 5 == 0); }

// 子节点相对 Y：让子底面恰好接触父顶面
// parentScale = S_p, childScale = S_c
// 父顶面 = P.y + S_p/2, 子中心 = 父顶面 + S_c/2
// localY = (子中心 - P.y) / S_p = 0.5 * (1 + S_c/S_p)
inline float ChildLocalY(float parentScale, float childScale)
{
    return 0.5f * (1.0f + childScale / parentScale);
}

// ===================================================================
// 道路块填充
// ===================================================================
int PopulateRoadChunk(std::vector<Scene::SceneObject>& objs, int cx, int cz,
                      int density, std::mt19937& rng)
{
    const float x0 = -kWorldHalf + static_cast<float>(cx) * kChunkSize;
    const float z0 = -kWorldHalf + static_cast<float>(cz) * kChunkSize;
    const float cxCenter = x0 + kChunkSize * 0.5f;
    const float czCenter = z0 + kChunkSize * 0.5f;

    std::uniform_real_distribution<float> urot(0.0f, 360.0f);
    std::uniform_int_distribution<int>   ukind(0, 15);
    std::uniform_real_distribution<float> uscale(0.8f, 1.2f);

    int added = 0;
    const int dynamicQuota = static_cast<int>(static_cast<float>(density) * kDynamicRatio);
    const int staticQuota  = density - dynamicQuota;

    // ---- 1. 地面基座：深灰沥青（scale=10 大立方体，顶面贴地 y≈0） ----
    objs.push_back(MakeProp(glm::vec3(cxCenter, -4.95f, czCenter), 10.0f,
                            kPalette[0], 0.0f, 0.0f, 0, 0.15f, 0.92f, -1));
    ++added;

    // ---- 2. 路沿石：4 个边缘小立方体 ----
    const float curbScale = 0.6f;
    const float curbY = -4.95f + ChildLocalY(10.0f, curbScale) * 10.0f; // 放在地面上方
    const float offset = kChunkSize * 0.5f - curbScale * 0.5f;
    objs.push_back(MakeProp(glm::vec3(cxCenter - offset, curbY, czCenter - offset), curbScale,
                            kPalette[8], 0.0f, 0.0f, 0, 0.1f, 0.85f, -1));
    objs.push_back(MakeProp(glm::vec3(cxCenter + offset, curbY, czCenter - offset), curbScale,
                            kPalette[8], 0.0f, 0.0f, 0, 0.1f, 0.85f, -1));
    objs.push_back(MakeProp(glm::vec3(cxCenter - offset, curbY, czCenter + offset), curbScale,
                            kPalette[8], 0.0f, 0.0f, 0, 0.1f, 0.85f, -1));
    objs.push_back(MakeProp(glm::vec3(cxCenter + offset, curbY, czCenter + offset), curbScale,
                            kPalette[8], 0.0f, 0.0f, 0, 0.1f, 0.85f, -1));
    added += 4;

    // ---- 3. 十字路口标记（黄色小方块） ----
    if (IsCrossroad(cx, cz))
    {
        objs.push_back(MakeProp(glm::vec3(cxCenter, -4.95f + ChildLocalY(10.0f, 0.5f) * 10.0f, czCenter),
                                0.5f, kPalette[6], 0.0f, 0.0f, 0, 0.0f, 0.3f, -1));
        ++added;
    }

    // ---- 4. 路灯：反重力悬浮灯球（赛博朋克风格，无灯杆） ----
    // 每隔 2 个道路 chunk 放一个路灯
    if (((cx + cz) & 1) == 0)
    {
        const float lampScale = 0.35f + 0.15f * HashFloat(cx, cz, 0, 7);
        const float lampY = 4.0f + 1.5f * HashFloat(cx, cz, 0, 8);
        objs.push_back(MakeProp(glm::vec3(cxCenter, lampY, czCenter), lampScale,
                                kPalette[12], 0.0f, 0.0f, 3, 0.0f, 0.2f, -1));
        ++added;
    }

    // ---- 5. 静态细节：井盖、碎石、交通标记（小胶囊/立方体） ----
    for (int i = 0; i < staticQuota && i < 6; ++i)
    {
        const float px = x0 + HashFloat(cx, cz, i, 1) * kChunkSize;
        const float pz = z0 + HashFloat(cx, cz, i, 2) * kChunkSize;
        const int   ftype = static_cast<int>(HashFloat(cx, cz, i, 3) * 3.0f); // 0=井盖,1=碎石,2=标记
        const float s = 0.15f + 0.25f * HashFloat(cx, cz, i, 4);

        if (ftype == 0)
        {
            // 井盖：小胶囊，金属色
            objs.push_back(MakeProp(glm::vec3(px, -4.95f + ChildLocalY(10.0f, s) * 10.0f, pz), s,
                                    kPalette[7] * 0.8f, 0.0f, 0.0f, 4, 0.6f, 0.4f, -1));
        }
        else if (ftype == 1)
        {
            // 碎石：小立方体
            objs.push_back(MakeProp(glm::vec3(px, -4.95f + ChildLocalY(10.0f, s) * 10.0f, pz), s,
                                    kPalette[8] * 0.7f, 0.0f, 0.0f, 0, 0.1f, 0.9f, -1));
        }
        else
        {
            // 交通标记：小白方块
            objs.push_back(MakeProp(glm::vec3(px, -4.95f + ChildLocalY(10.0f, s) * 10.0f, pz), s,
                                    kPalette[10], 0.0f, 0.0f, 0, 0.0f, 0.3f, -1));
        }
        ++added;
    }

    // ---- 6. 动态实体：雨水小球（spinSpeed ≠ 0，模拟风中旋转） ----
    for (int i = 0; i < dynamicQuota; ++i)
    {
        const float px = x0 + HashFloat(cx, cz, staticQuota + i, 5) * kChunkSize;
        const float pz = z0 + HashFloat(cx, cz, staticQuota + i, 6) * kChunkSize;
        const float py = 3.0f + 4.0f * HashFloat(cx, cz, staticQuota + i, 7);
        const float s  = 0.04f + 0.04f * HashFloat(cx, cz, staticQuota + i, 8);
        const float speed = 30.0f + 90.0f * HashFloat(cx, cz, staticQuota + i, 9);
        objs.push_back(MakeProp(glm::vec3(px, py, pz), s,
                                kPalette[4] * 0.6f + kPalette[13] * 0.4f,
                                speed, urot(rng), 3, 0.0f, 0.3f, -1));
        ++added;
    }

    return added;
}

// ===================================================================
// 建筑块填充
// ===================================================================
int PopulateBuildingChunk(std::vector<Scene::SceneObject>& objs, int cx, int cz,
                          int density, float centerDist, std::mt19937& rng)
{
    const float x0 = -kWorldHalf + static_cast<float>(cx) * kChunkSize;
    const float z0 = -kWorldHalf + static_cast<float>(cz) * kChunkSize;
    const float cxCenter = x0 + kChunkSize * 0.5f;
    const float czCenter = z0 + kChunkSize * 0.5f;

    std::uniform_real_distribution<float> urot(0.0f, 360.0f);
    std::uniform_int_distribution<int>   ukind(0, 15);

    int added = 0;
    const int dynamicQuota = static_cast<int>(static_cast<float>(density) * kDynamicRatio);
    const int staticQuota  = density - dynamicQuota;

    // ---- 1. 地面基座：混凝土/人行道色 ----
    const int groundTintIdx = ((cx + cz) & 1) ? 8 : 9; // 棋盘格混凝土/红
    objs.push_back(MakeProp(glm::vec3(cxCenter, -4.95f, czCenter), 10.0f,
                            kPalette[groundTintIdx], 0.0f, 0.0f, 0, 0.1f, 0.9f, -1));
    ++added;

    // ---- 2. 建筑生成（体素化堆叠，2~3 层父子链） ----
    // 建筑数量按密度层调整：Near=3~4 栋，Mid=2~3，Far=1~2，Outer=0~1
    int buildingCount = 1;
    if (centerDist < kNearRadius)      buildingCount = 3 + static_cast<int>(HashFloat(cx, cz, 0, 10) * 2.0f);
    else if (centerDist < kMidRadius)  buildingCount = 2 + static_cast<int>(HashFloat(cx, cz, 0, 10) * 2.0f);
    else if (centerDist < kFarRadius)  buildingCount = 1 + static_cast<int>(HashFloat(cx, cz, 0, 10) * 2.0f);
    else                               buildingCount = static_cast<int>(HashFloat(cx, cz, 0, 10) * 1.5f);

    int staticBudget = staticQuota;
    for (int b = 0; b < buildingCount && staticBudget > 0; ++b)
    {
        // 建筑位置：chunk 内随机偏移
        const float bx = cxCenter + (HashFloat(cx, cz, b, 11) - 0.5f) * kChunkSize * 0.6f;
        const float bz = czCenter + (HashFloat(cx, cz, b, 12) - 0.5f) * kChunkSize * 0.6f;

        // 建筑高度按层调整：Near 更高（摩天楼 12~20 m）
        float heightScale = 4.0f;
        if (centerDist < kNearRadius)      heightScale = 12.0f + 8.0f * HashFloat(cx, cz, b, 13);
        else if (centerDist < kMidRadius)  heightScale = 6.0f + 6.0f * HashFloat(cx, cz, b, 13);
        else if (centerDist < kFarRadius)  heightScale = 3.0f + 4.0f * HashFloat(cx, cz, b, 13);
        else                               heightScale = 2.0f + 2.0f * HashFloat(cx, cz, b, 13);

        // 建筑宽度（比高度窄，形成塔楼感）
        const float widthScale = 2.0f + 3.0f * HashFloat(cx, cz, b, 14);
        const glm::vec3 baseTint = kPalette[ukind(rng)];

        // ---- 建筑基座（第 1 层，parent=-1） ----
        const int baseIdx = static_cast<int>(objs.size());
        objs.push_back(MakeProp(glm::vec3(bx, widthScale * 0.5f, bz), widthScale,
                                baseTint * 0.8f, 0.0f, 0.0f, 0, 0.3f, 0.5f, -1));
        ++added; --staticBudget;

        // ---- 建筑主体（第 2 层，parent=base） ----
        if (heightScale > 2.0f && staticBudget > 0)
        {
            const float bodyScale = widthScale * (0.6f + 0.3f * HashFloat(cx, cz, b, 15));
            const float bodyLocalY = ChildLocalY(widthScale, bodyScale);
            const int bodyIdx = static_cast<int>(objs.size());
            objs.push_back(MakeProp(glm::vec3(0.0f, bodyLocalY, 0.0f), bodyScale,
                                    baseTint, 0.0f, 0.0f, 0, 0.3f, 0.45f, baseIdx));
            ++added; --staticBudget;

            // ---- 建筑顶部/天线（第 3 层，parent=body） ----
            if (heightScale > 4.0f && staticBudget > 0)
            {
                const float topScale = bodyScale * (0.4f + 0.3f * HashFloat(cx, cz, b, 16));
                const float topLocalY = ChildLocalY(bodyScale, topScale);
                objs.push_back(MakeProp(glm::vec3(0.0f, topLocalY, 0.0f), topScale,
                                        baseTint * 1.2f, 0.0f, 0.0f, 0, 0.3f, 0.4f, bodyIdx));
                ++added; --staticBudget;
            }
        }
    }

    // ---- 3. 霓虹招牌（1~2 个立方体，随机发光色） ----
    const int signCount = static_cast<int>(HashFloat(cx, cz, 0, 17) * 2.5f);
    for (int s = 0; s < signCount && staticBudget > 0; ++s)
    {
        const float sx = cxCenter + (HashFloat(cx, cz, s, 18) - 0.5f) * kChunkSize * 0.5f;
        const float sz = czCenter + (HashFloat(cx, cz, s, 19) - 0.5f) * kChunkSize * 0.5f;
        const float signScale = 0.8f + 0.7f * HashFloat(cx, cz, s, 20);
        const int   colorIdx  = static_cast<int>(HashFloat(cx, cz, s, 21) * 4.0f) % 4;
        const bool  dynamic   = (HashFloat(cx, cz, s, 22) < 0.3f); // 30% 招牌带自转（模拟闪烁）
        objs.push_back(MakeProp(glm::vec3(sx, 2.0f + signScale * 0.5f, sz), signScale,
                                kLightColors[colorIdx],
                                dynamic ? (20.0f + 40.0f * HashFloat(cx, cz, s, 23)) : 0.0f,
                                0.0f, 0, 0.0f, 0.25f, -1));
        ++added; --staticBudget;
    }

    // ---- 4. 管道/空调外机（小立方体/胶囊体，挂在建筑侧面） ----
    const int detailCount = 1 + static_cast<int>(HashFloat(cx, cz, 0, 24) * 3.0f);
    for (int d = 0; d < detailCount && staticBudget > 0; ++d)
    {
        const float dx = cxCenter + (HashFloat(cx, cz, d, 25) - 0.5f) * kChunkSize * 0.7f;
        const float dz = czCenter + (HashFloat(cx, cz, d, 26) - 0.5f) * kChunkSize * 0.7f;
        const float ds = 0.3f + 0.4f * HashFloat(cx, cz, d, 27);
        const int   mesh = (HashFloat(cx, cz, d, 28) < 0.5f) ? 0 : 4; // 立方体或胶囊
        objs.push_back(MakeProp(glm::vec3(dx, 1.0f + ds * 0.5f, dz), ds,
                                kPalette[7] * 0.9f, 0.0f, 0.0f, mesh, 0.5f, 0.6f, -1));
        ++added; --staticBudget;
    }

    // ---- 5. 全息投影球（动态，悬浮在空中） ----
    for (int i = 0; i < dynamicQuota; ++i)
    {
        const float hx = cxCenter + (HashFloat(cx, cz, i, 29) - 0.5f) * kChunkSize * 0.4f;
        const float hz = czCenter + (HashFloat(cx, cz, i, 30) - 0.5f) * kChunkSize * 0.4f;
        const float hy = 3.0f + 3.0f * HashFloat(cx, cz, i, 31);
        const float hs = 0.15f + 0.25f * HashFloat(cx, cz, i, 32);
        const int   hcol = 13 + static_cast<int>(HashFloat(cx, cz, i, 33) * 3.0f) % 3;
        const float speed = 45.0f + 90.0f * HashFloat(cx, cz, i, 34);
        objs.push_back(MakeProp(glm::vec3(hx, hy, hz), hs,
                                kPalette[hcol], speed, urot(rng), 3, 0.0f, 0.3f, -1));
        ++added;
    }

    return added;
}
} // namespace

// ===================================================================
// 构建开放世界场景
// ===================================================================
std::vector<Scene::SceneObject> BuildOpenWorldScene()
{
    std::vector<Scene::SceneObject> objs;
    objs.reserve(static_cast<size_t>(kTotalChunks * kNearDensity));

    for (int cz = 0; cz < kChunkCells; ++cz)
    {
        for (int cx = 0; cx < kChunkCells; ++cx)
        {
            const float centerX = -kWorldHalf + (static_cast<float>(cx) + 0.5f) * kChunkSize;
            const float centerZ = -kWorldHalf + (static_cast<float>(cz) + 0.5f) * kChunkSize;
            const float dist = std::sqrt(centerX * centerX + centerZ * centerZ);
            const int density = DensityForChunk(dist);
            if (density <= 0)
                continue;

            const uint32_t chunkSeed = kRngSeed
                                       + static_cast<uint32_t>(cx) * 73856093u
                                       + static_cast<uint32_t>(cz) * 19349663u;
            std::mt19937 rng(chunkSeed);

            if (IsRoadChunk(cx, cz))
                (void)PopulateRoadChunk(objs, cx, cz, density, rng);
            else
                (void)PopulateBuildingChunk(objs, cx, cz, density, dist, rng);
        }
    }

    return objs;
}

// ===================================================================
// 统计计算（纯函数，与旧版逻辑一致）
// ===================================================================
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

    // 动态子树节点统计：对每个 spinSpeed != 0 的节点，统计其整棵子树大小
    for (size_t di = 0; di < objs.size(); ++di)
    {
        if (objs[di].spinSpeed == 0.0f)
            continue;
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

    // Chunk 密度层统计
    for (int cz = 0; cz < kChunkCells; ++cz)
    {
        for (int cx = 0; cx < kChunkCells; ++cx)
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
