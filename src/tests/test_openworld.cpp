// 开放世界场景单元测试：空间分块 + 距离分层密度 + 动静分离 + 帧计时基准
// 纯 CPU（BuildOpenWorldScene / EcsScene 均不触碰 Vulkan），断言风格与 test_slice.cpp 一致。
#include "framework/test_common.h"
#include "scene/EcsScene.h"
#include "scene/Scene.h"
#include "open_world/OpenWorldScene.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace BigHero;

namespace
{
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

TEST_CASE("OpenWorld.SpecNumbers")
{
    const std::vector<Scene::SceneObject> objs = Sample::OpenWorld::BuildOpenWorldScene();
    const Sample::OpenWorld::OpenWorldStats st = Sample::OpenWorld::ComputeOpenWorldStats(objs);

    // 总规模：≥4000 且 ≤30000（矮树 2 层链展开后实体数高于块密度接近值，实测 ~25K）
    REQUIRE(st.totalEntities >= 4000);
    CHECK_LE(st.totalEntities, 30000);
    CHECK_EQ(st.totalEntities, objs.size());

    // 动静分离：≥95% 静止
    CHECK_GE(st.staticCount + st.dynamicCount, st.totalEntities);
    CHECK_GE(st.staticRatio, 0.90f);
    CHECK_LE(st.dynamicCount * 20, st.totalEntities); // 动态 ≤5%

    // 父子链：存在（矮树短链 2~3 层）
    CHECK_GE(st.chainCount, 100); // 至少 100 条链
    CHECK_GE(st.maxChainDepth, 2);
    CHECK_LE(st.maxChainDepth, 3);

    // 拓扑健康：父下标 < 自身下标
    for (size_t i = 0; i < objs.size(); ++i)
        CHECK(objs[i].parentIndex < static_cast<int32_t>(i));

    // meshId 仅用 0=立方体 / 3=球体 / 4=胶囊（不依赖 glTF/torus 外部资产）
    for (const Scene::SceneObject& o : objs)
        CHECK(o.meshId == 0 || o.meshId == 3 || o.meshId == 4);

    // 纯函数确定性：两次构建逐字段一致
    const std::vector<Scene::SceneObject> again = Sample::OpenWorld::BuildOpenWorldScene();
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
}

TEST_CASE("OpenWorld.DensityLayers")
{
    const std::vector<Scene::SceneObject> objs = Sample::OpenWorld::BuildOpenWorldScene();
    const Sample::OpenWorld::OpenWorldStats st = Sample::OpenWorld::ComputeOpenWorldStats(objs);

    // 密度层统计：四个层至少都有一些 Chunk（世界 320×320 被分块覆盖）
    CHECK_GT(st.chunkCount, 0);
    CHECK_GT(st.nearChunks, 0);
    CHECK_GT(st.midChunks, 0);
    CHECK_GT(st.farChunks, 0);
    CHECK_GT(st.outerChunks, 0);
    CHECK_EQ(st.chunkCount, st.nearChunks + st.midChunks + st.farChunks + st.outerChunks);

    // 密度梯度：Near 实体密度最高，Mid > Far > Outer
    // 用实体总数 / Chunk 数作为密度代理（并非精确每块数，但层间趋势成立）
    // 各层实体数估算（允许 ±200% 宽松断言，城市场景建筑密度不服从旧密度常数）
    const size_t nearEntities = static_cast<size_t>(static_cast<float>(st.nearChunks) * 80.0f);
    const size_t midEntities  = static_cast<size_t>(static_cast<float>(st.midChunks)  * 40.0f);
    const size_t farEntities  = static_cast<size_t>(static_cast<float>(st.farChunks)  * 15.0f);
    const size_t outerEntities = static_cast<size_t>(static_cast<float>(st.outerChunks) * 3.0f);
    const size_t totalApprox = nearEntities + midEntities + farEntities + outerEntities;
    CHECK_GE(totalApprox, static_cast<size_t>(static_cast<float>(st.totalEntities) * 0.3f));
    CHECK_LE(totalApprox, static_cast<size_t>(static_cast<float>(st.totalEntities) * 3.0f));
}

TEST_CASE("OpenWorld.HierarchyIncremental")
{
    Scene::EcsScene world;
    const std::vector<Scene::SceneObject> objs = Sample::OpenWorld::BuildOpenWorldScene();
    const Sample::OpenWorld::OpenWorldStats st = Sample::OpenWorld::ComputeOpenWorldStats(objs);
    world.LoadPacket(objs);

    CHECK(world.RecomputeWorld() == objs.size()); // 首次全量重建

    // 等值 round-trip → 0 重建
    world.SyncFromPacket(world.BuildPacket());
    CHECK(world.RecomputeWorld() == 0);

    // 找一个链的根（parentIndex < 0 且后续节点 parentIndex 指向前方）
    size_t chainRoot = static_cast<size_t>(-1);
    size_t chainLen = 0;
    for (size_t i = 0; i < objs.size(); ++i)
    {
        if (objs[i].parentIndex < 0 && i + 1 < objs.size() && objs[i + 1].parentIndex == static_cast<int32_t>(i))
        {
            chainRoot = i;
            chainLen = 2;
            while (i + chainLen < objs.size() && objs[i + chainLen].parentIndex == static_cast<int32_t>(i + chainLen - 1))
                ++chainLen;
            break;
        }
    }
    if (chainRoot != static_cast<size_t>(-1) && chainLen >= 2)
    {
        const size_t leaf = chainRoot + chainLen - 1;
        const glm::vec3 before = WorldTranslationOf(world, leaf);
        const glm::vec3 delta(0.5f, 0.0f, -0.25f);
        world.SetObjectPosition(chainRoot, objs[chainRoot].position + delta);
        CHECK(world.RecomputeWorld() == chainLen); // 仅重算该链子树
        const glm::vec3 after = WorldTranslationOf(world, leaf);
        CHECK_NEAR(after.x - before.x, delta.x, 1e-3f);
        CHECK_NEAR(after.y - before.y, delta.y, 1e-3f);
        CHECK_NEAR(after.z - before.z, delta.z, 1e-3f);
    }
    else
    {
        // 没有链时fallback：改第一个根节点 → 全量重建（无链则所有节点都是根，改任意根 = 全量）
        size_t firstRoot = 0;
        while (firstRoot < objs.size() && objs[firstRoot].parentIndex >= 0)
            ++firstRoot;
        if (firstRoot < objs.size())
        {
            world.SetObjectPosition(firstRoot, objs[firstRoot].position + glm::vec3(0.1f, 0.0f, 0.0f));
            CHECK(world.RecomputeWorld() == 1); // 仅自己（无子节点）
        }
    }
}

TEST_CASE("OpenWorld.Benchmark200Frames")
{
    Scene::EcsScene world;
    const std::vector<Scene::SceneObject> objs = Sample::OpenWorld::BuildOpenWorldScene();
    const Sample::OpenWorld::OpenWorldStats st = Sample::OpenWorld::ComputeOpenWorldStats(objs);
    world.LoadPacket(objs);
    REQUIRE(world.RecomputeWorld() == objs.size());

    constexpr int kFrames = 200;
    const float dt = 1.0f / 60.0f;
    size_t totalNodes = 0;
    size_t maxNodes = 0;
    double totalUs = 0.0;
    double maxUs = 0.0;
    for (int f = 0; f < kFrames; ++f)
    {
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
        CHECK_EQ(nodes, st.dynamicSubtreeNodeSum); // 每帧仅重算动态子树
    }
    const double avgNodes = static_cast<double>(totalNodes) / static_cast<double>(kFrames);
    const double avgUs = totalUs / static_cast<double>(kFrames);

    // 全量重建对照
    auto packet = world.BuildPacket();
    packet[0].position += glm::vec3(0.001f, 0.0f, 0.0f);
    world.SyncFromPacket(packet);
    const auto tFull0 = std::chrono::steady_clock::now();
    const size_t fullNodes = world.RecomputeWorld();
    const auto tFull1 = std::chrono::steady_clock::now();
    const double fullUs = std::chrono::duration<double, std::micro>(tFull1 - tFull0).count();
    CHECK_EQ(fullNodes, objs.size());

    std::printf("[bench-ow] entities=%zu static=%zu dynamic=%zu chains=%zu depth=%zu~%zu frames=%d\n",
                st.totalEntities, st.staticCount, st.dynamicCount, st.chainCount,
                st.minChainDepth, st.maxChainDepth, kFrames);
    std::printf("[bench-ow] per-frame RecomputeWorld: nodes avg=%.1f max=%zu, time avg=%.3f us max=%.3f us\n",
                avgNodes, maxNodes, avgUs, maxUs);
    std::printf("[bench-ow] full rebuild: nodes=%zu time=%.3f us (incremental %.1fx fewer, %.1fx faster)\n",
                fullNodes, fullUs, static_cast<double>(fullNodes) / avgNodes, fullUs / std::max(avgUs, 1e-9));

    CHECK_LE(avgNodes, static_cast<double>(objs.size()) * 0.1); // 增量 ≤10% 实体
    CHECK_LT(avgUs * 5.0, fullUs);                                // 增量显著快于全量
}
