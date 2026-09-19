// 垂直切片场景构建（实现）：见 SliceScene.h 的规格说明。
// 仅依赖 scene/Scene.h + glm（纯 CPU、确定性、可离线单测，不触碰 Vulkan/GPU）。
#include "vertical_slice/SliceScene.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace BigHero::Sample::VerticalSlice
{
namespace
{
// ---- 布局常量（1200 = 塔 192 + 自转环 50 + 外圈散布 958） ----
constexpr int kTowerCols = 10;  // 塔群列数
constexpr int kTowerRows = 5;   // 塔群行数
constexpr int kTowerCount = 50; // 父子链（塔）数量
constexpr float kTowerSpacingX = 6.0f;
constexpr float kTowerSpacingZ = 7.0f;
constexpr int kRingCount = 50;       // 自转环实体数
constexpr float kRingRadius = 40.0f; // 自转环半径（塔群外缘 ~±29，环在散布网格上方悬浮）
constexpr float kRingHeight = 1.8f;  // 自转环悬浮高度（散布道具顶部 <=1.0，无碰撞）
constexpr int kScatterCols = 40;     // 散布网格列数
constexpr int kScatterRows = 24;     // 散布网格行数（40*24 - 2 个预留空位 = 958）
constexpr float kScatterSpacing = 3.0f;
constexpr size_t kTotalEntities = 1200;

// 塔的层数分布：前 18 座 3 层、中间 22 座 4 层、末尾 10 座 5 层（3~5 层规格）。
constexpr int kShallowTowers = 18; // 3 层
constexpr int kMidTowers = 22;     // 4 层（含自转顶饰的前 5 座 + 第 4 层自转的后 5 座）

// 6 色调色板（与默认场景的 PBR 展示风格一致：电介质/金属、粗糙度渐变）
constexpr glm::vec3 kPalette[6] = {
    {1.0f, 1.0f, 1.0f},   {0.60f, 0.78f, 1.0f},  {1.0f, 0.60f, 0.40f},
    {0.60f, 1.0f, 0.68f}, {0.88f, 0.60f, 0.98f}, {1.0f, 0.77f, 0.34f},
};

// 组装一个场景物体（SceneObject 的标量字段无默认初始化器，显式赋全保证确定性）。
// 物理默认 None（切片场景聚焦渲染/层级，不参与物理模拟）。
Scene::SceneObject MakeProp(const glm::vec3& position, float scale, const glm::vec3& tint, float spinSpeed, float phase,
                            uint32_t meshId, float metallic, float roughness, int32_t parentIndex)
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

// 组装一座塔（一条父子链）：底座→柱身→…→顶饰逐层堆叠，返回节点数。
// 层段规格：L0 底座立方体 1.2 / L1 柱身立方体 0.9 / L2 球体 0.7 / L3 立方体 0.55 / L4 顶饰球体 0.4。
// 立方体与球体的半高均为 scale/2（单位立方体边长 1、单位球半径 0.5），据此精确堆叠无穿插。
// spinLevel：自转层号（-1=无自转）；父节点先于子节点入包（parentIndex < 自身下标）。
int AppendTower(std::vector<Scene::SceneObject>& objs, int towerIndex, float x, float z, int levels, int spinLevel)
{
    static constexpr int kTowerMesh[5] = {0, 0, 3, 0, 3}; // L0..L4：柱/柱/球/柱/球
    static constexpr float kTowerScale[5] = {1.2f, 0.9f, 0.7f, 0.55f, 0.4f};
    static constexpr float kTowerSpeed[2] = {60.0f, 45.0f}; // 顶饰自转 / 中层自转（带动悬挂子节点）

    const glm::vec3 baseTint = kPalette[towerIndex % 6];
    const float metallic = (towerIndex % 3 == 0) ? 0.85f : 0.1f;
    const float roughness = 0.2f + 0.12f * static_cast<float>(towerIndex % 6);

    // 根节点（底座）：局部坐标 == 世界坐标（无父）
    float centerY = 0.5f * kTowerScale[0]; // 底面贴地
    int rootIndex = static_cast<int>(objs.size());
    objs.push_back(MakeProp(glm::vec3(x, centerY, z), kTowerScale[0], baseTint * 0.85f,
                            spinLevel == 0 ? kTowerSpeed[towerIndex % 2] : 0.0f, static_cast<float>(towerIndex) * 11.0f,
                            static_cast<uint32_t>(kTowerMesh[0]), metallic, roughness, -1));
    int parentIndex = rootIndex;
    float parentHalf = 0.5f * kTowerScale[0];
    for (int level = 1; level < levels; ++level)
    {
        const float half = 0.5f * kTowerScale[level];
        centerY += parentHalf + half; // 堆叠在父段顶面
        objs.push_back(MakeProp(glm::vec3(0.0f, parentHalf + half, 0.0f), kTowerScale[level], baseTint,
                                spinLevel == level ? kTowerSpeed[towerIndex % 2] : 0.0f,
                                static_cast<float>(towerIndex) * 7.0f + static_cast<float>(level) * 13.0f,
                                static_cast<uint32_t>(kTowerMesh[level]), metallic, roughness, parentIndex));
        parentIndex = static_cast<int>(objs.size()) - 1;
        parentHalf = half;
    }
    return levels;
}
} // namespace

std::vector<Scene::SceneObject> BuildSliceScene()
{
    std::vector<Scene::SceneObject> objs;
    objs.reserve(kTotalEntities);

    // ---- 1) 中央塔群：50 条父子链（192 节点），父节点先于子节点入包 ----
    for (int t = 0; t < kTowerCount; ++t)
    {
        const int col = t % kTowerCols;
        const int row = t / kTowerCols;
        const float x =
            -0.5f * static_cast<float>(kTowerCols - 1) * kTowerSpacingX + static_cast<float>(col) * kTowerSpacingX;
        const float z =
            -0.5f * static_cast<float>(kTowerRows - 1) * kTowerSpacingZ + static_cast<float>(row) * kTowerSpacingZ;
        // 层数：前 18 座 3 层 → 中间 22 座 4 层 → 末尾 10 座 5 层
        const int levels = (t < kShallowTowers) ? 3 : (t < kShallowTowers + kMidTowers ? 4 : 5);
        // 末尾 10 座（5 层）承担层级自转：前 5 座顶饰自转（叶），后 5 座第 4 层自转（带悬挂子节点）
        const int spinLevel =
            (t < kShallowTowers + kMidTowers) ? -1 : (t < kShallowTowers + kMidTowers + 5 ? levels - 1 : levels - 2);
        (void)AppendTower(objs, t, x, z, levels, spinLevel);
    }

    // ---- 2) 自转环：50 个悬浮自转立方体（全部为根节点，非链成员） ----
    for (int i = 0; i < kRingCount; ++i)
    {
        const float theta = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(kRingCount);
        const glm::vec3 pos(kRingRadius * std::cos(theta), kRingHeight, kRingRadius * std::sin(theta));
        objs.push_back(MakeProp(pos, 0.8f, kPalette[i % 6], 24.0f + 12.0f * static_cast<float>(i % 6),
                                13.7f * static_cast<float>(i), 0, 0.7f, 0.25f, -1));
    }

    // ---- 3) 外圈散布：958 个静止道具（立方体/球体/胶囊对角带轮换，尺寸确定性变化） ----
    // 预留 2 个空位（对角）让总数精确到 1200：跳过 (0,0) 与 (39,23)。
    const float x0 = -0.5f * static_cast<float>(kScatterCols - 1) * kScatterSpacing;
    const float z0 = -0.5f * static_cast<float>(kScatterRows - 1) * kScatterSpacing;
    for (int r = 0; r < kScatterRows; ++r)
    {
        for (int c = 0; c < kScatterCols; ++c)
        {
            if ((c == 0 && r == 0) || (c == kScatterCols - 1 && r == kScatterRows - 1))
                continue; // 预留空位（数量配平）
            const float x = x0 + static_cast<float>(c) * kScatterSpacing;
            const float z = z0 + static_cast<float>(r) * kScatterSpacing;
            const uint32_t meshId = static_cast<uint32_t>((c + r) % 3 == 0 ? 0 : ((c + r) % 3 == 1 ? 3 : 4));
            const float scale = 0.5f + 0.125f * static_cast<float>((c * 7 + r * 13) % 5);
            // 单位胶囊总高 1.7（半径 0.5 + 柱高 0.7）→ 贴地 y=0.85*scale；立方体/球体 y=0.5*scale
            const float y = (meshId == 4) ? 0.85f * scale : 0.5f * scale;
            objs.push_back(MakeProp(glm::vec3(x, y, z), scale, kPalette[(c * 3 + r) % 6] * 0.9f, 0.0f, 0.0f, meshId,
                                    ((c + r) % 4 == 0) ? 0.9f : 0.0f, 0.35f + 0.1f * static_cast<float>((c + r) % 4),
                                    -1));
        }
    }

    return objs;
}

SliceSceneStats ComputeSliceStats(const std::vector<Scene::SceneObject>& objs)
{
    SliceSceneStats st;
    st.totalEntities = objs.size();
    const size_t n = objs.size();
    if (n == 0)
    {
        st.staticRatio = 1.0f;
        return st;
    }

    // 静止/自转分类 + 子节点表（非法 parentIndex 视为根，与 EcsScene 语义一致）
    std::vector<std::vector<size_t>> children(n);
    std::vector<int32_t> parent(n, -1);
    for (size_t i = 0; i < n; ++i)
    {
        if (objs[i].spinSpeed != 0.0f)
            ++st.spinnerCount;
        else
            ++st.staticCount;
        const int32_t p = objs[i].parentIndex;
        if (p >= 0 && static_cast<size_t>(p) < i)
        {
            parent[i] = p;
            children[static_cast<size_t>(p)].push_back(i);
        }
    }
    st.staticRatio = static_cast<float>(st.staticCount) / static_cast<float>(n);

    // 子树规模/层数 DP：父下标 < 子下标，逆序单趟（子先于父就绪）。
    std::vector<size_t> subtreeSize(n, 1);
    std::vector<size_t> subtreeDepth(n, 1); // 层数：自身为第 1 层
    for (size_t i = n; i-- > 0;)
    {
        for (const size_t c : children[i])
        {
            subtreeSize[i] += subtreeSize[c];
            subtreeDepth[i] = std::max(subtreeDepth[i], subtreeDepth[c] + 1);
        }
    }

    // 父子链 = 有子节点的根引领的最大子树；层数 = 子树最大深度。
    bool first = true;
    for (size_t i = 0; i < n; ++i)
    {
        if (parent[i] >= 0 || children[i].empty())
            continue;
        ++st.chainCount;
        if (first || subtreeDepth[i] < st.minChainDepth)
        {
            st.minChainDepth = subtreeDepth[i];
            st.shallowestChainRootIndex = i;
        }
        if (first || subtreeDepth[i] > st.maxChainDepth)
        {
            st.maxChainDepth = subtreeDepth[i];
            st.deepestChainRootIndex = i;
        }
        first = false;
    }

    // 自转实体子树节点数之和（每帧增量重算节点数的期望值；要求自转实体互不为祖先/后代）
    for (size_t i = 0; i < n; ++i)
    {
        if (objs[i].spinSpeed != 0.0f)
            st.spinnerSubtreeNodeSum += subtreeSize[i];
    }
    return st;
}
} // namespace BigHero::Sample::VerticalSlice
