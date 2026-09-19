#pragma once
// 垂直切片场景（Vertical Slice）：生产形态的「上千实体 + 大部分静止 + 真实父子层级」场景。
//
// 动机：
//   1. TransformHierarchy 脏标记增量路径的收益宣称（空闲帧 O(1)、修改帧仅重算受影响子树，
//      消除每帧容器重建）需要一个真实规模场景用帧计时证明——默认演示场景只有 6 物体且
//      全部自转，生产调用中 SetParent 为 0。
//   2. CI 成像回归需要稳定的目标场景（固定实体数/固定布局，确定性构建）。
//
// 场景规格（全部可经 ComputeSliceStats / 单元测试断言）：
//   - 1200 个实体，走既有实例化渲染路径（meshId 0=立方体 / 3=球体 / 4=胶囊，不依赖外部资产）；
//   - 95% 静止（仅 60 个实体有 Spin 自转，占比恰为 5%，满足 ≥90% 静止 / ≤5% 自转）；
//   - 50 条真实父子链（「塔」：底座→柱身→…→顶饰，经 SceneObject.parentIndex 挂接，
//     EcsScene::CreateObject 内部走 SetParent 生产路径），链层数 3~5 层；
//   - 布局确定性：纯函数构建，无全局状态，同输入逐字段一致，可离线单测。
//
// 布局总览：
//   - 中央塔群：10×5 网格（间距 6×7 m），50 座塔共 192 节点；
//     前 18 座 3 层、中间 22 座 4 层、末尾 10 座 5 层；
//     末尾 10 座中前 5 座顶饰自转（叶节点）、后 5 座第 4 层自转（带 1 个悬挂子节点）；
//   - 自转环：半径 40 m 处 50 个悬浮自转立方体（y=1.8，高于散布道具顶部，无碰撞）；
//   - 外圈散布：40×24 网格（间距 3 m）958 个静止道具（立方体/球体/胶囊按对角带轮换）。

#include "scene/Scene.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace BigHero::Sample::VerticalSlice
{
// 场景统计（由 SceneObject 包结构派生，与构建解耦的纯函数）。
struct SliceSceneStats
{
    size_t totalEntities = 0;            // 实体总数
    size_t staticCount = 0;              // 静止实体数（spinSpeed == 0）
    size_t spinnerCount = 0;             // 自转实体数（spinSpeed != 0）
    float staticRatio = 0.0f;            // 静止占比 staticCount / totalEntities
    size_t chainCount = 0;               // 父子链数（有子节点的根引领的子树）
    size_t minChainDepth = 0;            // 最短链层数（根为第 1 层）
    size_t maxChainDepth = 0;            // 最深链层数
    size_t deepestChainRootIndex = 0;    // 最深链的根（包下标）
    size_t shallowestChainRootIndex = 0; // 最短链的根（包下标）
    // 各自转实体子树节点数之和。BuildSliceScene 保证自转实体互不为祖先/后代，
    // 因此该值 == 每帧 UpdateSpins 后 RecomputeWorld 的增量重算节点数（帧计时基准的期望值）。
    size_t spinnerSubtreeNodeSum = 0;
};

// 构建垂直切片场景（纯函数：确定性输出，可离线单测）。
// 返回 SceneObject 包（父子经 parentIndex 表达，父下标 < 子下标），交由
// EcsScene::LoadPacket 一次性灌入（内部逐实体走 SetParent 生产路径）。
[[nodiscard]] std::vector<Scene::SceneObject> BuildSliceScene();

// 分析任意 SceneObject 包的静止占比与父子层级结构（纯函数）。
// 非法 parentIndex（越界/自环/后挂）按 EcsScene 语义视为根。
[[nodiscard]] SliceSceneStats ComputeSliceStats(const std::vector<Scene::SceneObject>& objs);
} // namespace BigHero::Sample::VerticalSlice
