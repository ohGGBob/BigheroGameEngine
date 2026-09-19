// 垂直切片场景单元测试：规格断言（实体数/静止占比/父子链拓扑）+ 层级增量重算行为
// + 1200 实体 × 200 帧基准（TransformHierarchy 脏标记增量的帧计时证据）。
// 纯 CPU（BuildSliceScene / EcsScene 均不触碰 Vulkan），断言风格与 test_ecs_scene.cpp 一致。
#include "framework/test_common.h"
#include "scene/EcsScene.h"
#include "scene/Scene.h"
#include "vertical_slice/SliceScene.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace BigHero;

namespace
{
// 收集稳定序第 idx 个实体的世界矩阵平移列（ForEachRenderableWorld 内部走 EnsureWorld，幂等）。
glm::vec3 WorldTranslationOf(const Scene::EcsScene& world, size_t idx)
{
    glm::vec3 translation(0.0f);
    size_t cursor = 0;
    world.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable&, const Scene::ecs::Spin&, const glm::mat4& w)
        {
            if (cursor == idx)
                translation = glm::vec3(w[3]);
            ++cursor;
        });
    return translation;
}
} // namespace

TEST_CASE("SliceScene.SpecNumbers")
{
    // ---- 场景规格：1200 实体 / 静止 ≥90% / 自转 ≤5% / ≥50 条父子链（3~5 层） ----
    const std::vector<Scene::SceneObject> objs = Sample::VerticalSlice::BuildSliceScene();
    const Sample::VerticalSlice::SliceSceneStats st = Sample::VerticalSlice::ComputeSliceStats(objs);

    REQUIRE(st.totalEntities >= 1200);
    CHECK_EQ(st.totalEntities, objs.size());
    CHECK_EQ(st.totalEntities, size_t{1200}); // CI 成像回归的固定目标规模
    CHECK_GE(st.staticCount + st.spinnerCount, st.totalEntities);
    CHECK_GE(st.staticRatio, 0.90f);                  // 静止占比 ≥90%
    CHECK_LE(st.spinnerCount * 20, st.totalEntities); // 自转实体 ≤5%（整数精确判定）
    CHECK_EQ(st.staticCount + st.spinnerCount, st.totalEntities);

    CHECK_GE(st.chainCount, 50); // 父子链数量
    CHECK_GE(st.minChainDepth, 3);
    CHECK_LE(st.maxChainDepth, 5);

    // 纯函数确定性：两次构建逐字段一致（布局/材质/父子关系全同）
    const std::vector<Scene::SceneObject> again = Sample::VerticalSlice::BuildSliceScene();
    REQUIRE(again.size() == objs.size());
    for (size_t i = 0; i < objs.size(); ++i)
    {
        CHECK(again[i].position == objs[i].position);
        CHECK(again[i].rotation == objs[i].rotation);
        CHECK(again[i].scale == objs[i].scale);
        CHECK(again[i].tint == objs[i].tint);
        CHECK(again[i].spinSpeed == objs[i].spinSpeed);
        CHECK(again[i].phase == objs[i].phase);
        CHECK(again[i].meshId == objs[i].meshId);
        CHECK(again[i].metallic == objs[i].metallic);
        CHECK(again[i].roughness == objs[i].roughness);
        CHECK(again[i].parentIndex == objs[i].parentIndex);
    }

    // 拓扑健康：父下标 < 自身下标（父先于子创建，防自环；EcsScene::CreateObject 挂接前提）
    for (size_t i = 0; i < objs.size(); ++i)
        CHECK(objs[i].parentIndex < static_cast<int32_t>(i));

    // 走既有实例化渲染路径：meshId 仅用 0=立方体 / 3=球体 / 4=胶囊（不依赖 torus/glTF 外部资产）
    for (const Scene::SceneObject& o : objs)
        CHECK(o.meshId == 0 || o.meshId == 3 || o.meshId == 4);
}

TEST_CASE("SliceScene.HierarchyIncremental")
{
    // ---- 增量行为：改一条链的根 → RecomputeWorld 重算数 == 该链节点数（断言风格同
    //      test_ecs_scene.cpp 的 SyncFromPacketCacheStaysClean） ----
    Scene::EcsScene world;
    const std::vector<Scene::SceneObject> objs = Sample::VerticalSlice::BuildSliceScene();
    const Sample::VerticalSlice::SliceSceneStats st = Sample::VerticalSlice::ComputeSliceStats(objs);
    world.LoadPacket(objs); // 父子经 parentIndex 走 CreateObject->SetParent 生产路径

    CHECK(world.RecomputeWorld() == objs.size()); // 首次预热：全量重建

    // 等值 round-trip（主循环每帧 SyncSceneEdits 路径）：不得置脏 → 0 重建
    world.SyncFromPacket(world.BuildPacket());
    CHECK(world.RecomputeWorld() == 0);

    // 最深链的根：链节点在包中连续（塔构建约定），逐层 parentIndex 直读验证
    const size_t deepRoot = st.deepestChainRootIndex;
    REQUIRE(deepRoot + st.maxChainDepth <= objs.size());
    for (size_t k = 1; k < st.maxChainDepth; ++k)
        CHECK(objs[deepRoot + k].parentIndex == static_cast<int32_t>(deepRoot + k - 1));
    const size_t topOfDeepChain = deepRoot + st.maxChainDepth - 1;

    // 改根（无旋转纯平移）→ 仅重算该链子树，其余 ~1195 实体复用缓存
    const glm::vec3 delta(0.25f, 0.5f, -0.125f);
    const glm::vec3 topBefore = WorldTranslationOf(world, topOfDeepChain);
    world.SetObjectPosition(deepRoot, objs[deepRoot].position + delta);
    CHECK(world.RecomputeWorld() == st.maxChainDepth);

    // 世界矩阵级联正确性：链顶端子节点的世界平移应随根平移同量移动
    const glm::vec3 topAfter = WorldTranslationOf(world, topOfDeepChain);
    CHECK_NEAR(topAfter.x - topBefore.x, delta.x, 1e-3f);
    CHECK_NEAR(topAfter.y - topBefore.y, delta.y, 1e-3f);
    CHECK_NEAR(topAfter.z - topBefore.z, delta.z, 1e-3f);

    // 幂等：无修改再驱动为 0
    CHECK(world.RecomputeWorld() == 0);

    // 最短链的根：重算数 == 该链层数
    world.SetObjectPosition(st.shallowestChainRootIndex, objs[st.shallowestChainRootIndex].position + delta);
    CHECK(world.RecomputeWorld() == st.minChainDepth);

    // 等值 round-trip 后缓存仍干净
    world.SyncFromPacket(world.BuildPacket());
    CHECK(world.RecomputeWorld() == 0);
}

TEST_CASE("SliceScene.Benchmark200Frames")
{
    // ---- 帧计时证据：1200 实体 × 200 帧（dt=1/60s），静止主导下每帧 RecomputeWorld
    //      仅重算自转实体子树（期望 = spinnerSubtreeNodeSum），对照 TRS 写回触发的全量重建。 ----
    Scene::EcsScene world;
    const std::vector<Scene::SceneObject> objs = Sample::VerticalSlice::BuildSliceScene();
    const Sample::VerticalSlice::SliceSceneStats st = Sample::VerticalSlice::ComputeSliceStats(objs);
    world.LoadPacket(objs);
    REQUIRE(world.RecomputeWorld() == objs.size()); // 预热：全量重建进入稳态

    constexpr int kFrames = 200;
    const float dt = 1.0f / 60.0f;
    size_t totalNodes = 0;
    size_t maxNodes = 0;
    double totalUs = 0.0;
    double maxUs = 0.0;
    for (int f = 0; f < kFrames; ++f)
    {
        // 生产帧形态：等值 round-trip（编辑器包投影写回，不置脏）→ 自转积分 → 层级驱动
        world.SyncFromPacket(world.BuildPacket());
        world.UpdateSpins(dt);
        const auto t0 = std::chrono::steady_clock::now();
        const size_t nodes = world.RecomputeWorld();
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        totalNodes += nodes;
        maxNodes = std::max(maxNodes, nodes);
        totalUs += us;
        maxUs = std::max(maxUs, us);
        CHECK_EQ(nodes, st.spinnerSubtreeNodeSum); // 每帧恰好只重算自转子树（round-trip 未置脏的证明）
    }
    const double avgNodes = static_cast<double>(totalNodes) / static_cast<double>(kFrames);
    const double avgUs = totalUs / static_cast<double>(kFrames);

    // 全量重建对照（编辑器/Gizmo TRS 写回置脏路径）：ResetMatrices + 1200 节点级联
    auto packet = world.BuildPacket();
    packet[0].position += glm::vec3(0.001f, 0.0f, 0.0f);
    world.SyncFromPacket(packet);
    const auto tFull0 = std::chrono::steady_clock::now();
    const size_t fullNodes = world.RecomputeWorld();
    const auto tFull1 = std::chrono::steady_clock::now();
    const double fullUs = std::chrono::duration<double, std::micro>(tFull1 - tFull0).count();
    CHECK_EQ(fullNodes, objs.size());

    // 报告数字（CI 日志可查）
    std::printf("[bench] entities=%zu static=%zu spinners=%zu chains=%zu depth=%zu~%zu frames=%d\n", st.totalEntities,
                st.staticCount, st.spinnerCount, st.chainCount, st.minChainDepth, st.maxChainDepth, kFrames);
    std::printf("[bench] per-frame RecomputeWorld: nodes avg=%.1f max=%zu, time avg=%.3f us max=%.3f us\n", avgNodes,
                maxNodes, avgUs, maxUs);
    std::printf(
        "[bench] full rebuild reference: nodes=%zu time=%.3f us (incremental %.1fx fewer nodes, %.1fx faster)\n",
        fullNodes, fullUs, static_cast<double>(fullNodes) / avgNodes, fullUs / std::max(avgUs, 1e-9));

    // 增量收益断言（确定性部分锁死，计时留足裕量防 CI 噪声）
    CHECK_LE(avgNodes, static_cast<double>(objs.size()) * 0.1); // 每帧重算 ≤10% 实体（实测 ~5.4%）
    CHECK_LT(avgUs * 5.0, fullUs);                              // 增量显著快于全量重建
}
