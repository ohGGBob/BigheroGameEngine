#pragma once
// 开放世界场景模板（Open World Template）：引擎在「大规模 ECS + 空间分块 + 距离分层密度」
// 场景下的能力验收基准。对标垂直切片（SliceScene），但规模放大 ~8x，引入距离分层与
// 空间分块概念，测试视锥剔除 + 层级缓存增量 + 实例化渲染在密集场景下的实际帧率。
//
// 动机：
//   1. SliceScene 1200 实体已验证层级缓存增量路径在 60fps 下收益显著，但 10K 实体级
//      规模才是生产型开放世界的入口门槛（植被群、岩石散布、远景简化）。
//   2. 距离分层密度模拟 LOD-0/LOD-1/LOD-2 的裁剪行为：近景密集、中景适中、远景稀疏、
//      超远景极简——即使无真实地形/几何 LOD，也能用实体数量梯度验证剔除管线效率。
//   3. 确定性构建：纯函数、无外部资产、离线单测可复现，CI 成像回归可用。
//
// 世界规格（全部可经 ComputeOpenWorldStats / 单元测试断言）：
//   - 世界尺寸：320 × 320 m（中心原点，四象限对称）
//   - 分块粒度：10 × 10 m → 32 × 32 = 1024 个 Chunk
//   - 密度层（距原点距离，每 Chunk 实体数）：
//       Near    0–40 m    Dense    80 实体/Chunk  → 约 1,280 实体（~20 个 Chunk 环形）
//       Mid    40–80 m    Medium   40 实体/Chunk  → 约 2,560 实体（~60 个 Chunk 环形）
//       Far    80–160 m   Sparse   15 实体/Chunk  → 约 2,880 实体（~192 个 Chunk 环形）
//       Outer 160–320 m   Minimal   3 实体/Chunk  → 约 1,536 实体（~512 个 Chunk 方形框）
//       Total ≈ 8,256 实体（目标 8K 级，留余量到 10K 上限）
//   - 实体类型：岩石（立方体）、矮树（立方体躯干 + 球体树冠）、草丛（小立方体）、
//     灌木（胶囊体）。仅用 meshId 0/3/4（不依赖 glTF/torus 外部资产）。
//   - 动静分离：≥95% 静止（spinSpeed == 0），≤5% 动态（“萤火虫”小球漂移 + 轻微上下浮动）。
//   - 父子层级：每 Chunk 随机 1~3 条短链（2~3 层），模拟植被的“根-干-冠”结构。
//     总链数 1024~3072 条，平均链长 2.5 层。
//   - 布局确定性：固定 RNG 种子（seed=42），纯函数构建，同输入逐字段一致。

#include "scene/Scene.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace BigHero::Sample::OpenWorld
{
// 场景统计（由 SceneObject 包结构派生，与构建解耦的纯函数）。
struct OpenWorldStats
{
    size_t totalEntities = 0;     // 实体总数
    size_t staticCount = 0;       // 静止实体数
    size_t dynamicCount = 0;      // 动态实体数（萤火虫漂移 + 轻微自转）
    float staticRatio = 0.0f;     // 静止占比
    size_t chainCount = 0;        // 父子链数
    size_t minChainDepth = 0;     // 最短链层数
    size_t maxChainDepth = 0;     // 最深链层数
    size_t chunkCount = 0;        // 被占用的 Chunk 数（含至少 1 个实体）
    size_t nearChunks = 0;        // Near 层 Chunk 数
    size_t midChunks = 0;         // Mid 层 Chunk 数
    size_t farChunks = 0;        // Far 层 Chunk 数
    size_t outerChunks = 0;      // Outer 层 Chunk 数
    // 动态实体子树节点数之和（每帧 RecomputeWorld 增量重算期望值）
    size_t dynamicSubtreeNodeSum = 0;
};

// 构建开放世界场景（纯函数：确定性输出，固定 seed=42，可离线单测）。
// 返回 SceneObject 包（父子经 parentIndex 表达，父下标 < 子下标），交由
// EcsScene::LoadPacket 一次性灌入（内部逐实体走 SetParent 生产路径）。
[[nodiscard]] std::vector<Scene::SceneObject> BuildOpenWorldScene();

// 分析任意 SceneObject 包的开放世界统计（纯函数）。
// 非法 parentIndex（越界/自环/后挂）按 EcsScene 语义视为根。
[[nodiscard]] OpenWorldStats ComputeOpenWorldStats(const std::vector<Scene::SceneObject>& objs);
} // namespace BigHero::Sample::OpenWorld