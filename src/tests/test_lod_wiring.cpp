// LOD 接线（选档 → 分桶映射）单元测试：纯逻辑、零 GPU，可离线运行。
// 复刻生产 Application::UpdateRenderables 对 meshId=3(球)/4(胶囊) 的分桶口径：
//   screenH = R / (d * tanHalfFov)   →   level = LodGroup::SelectLevel(screenH)
//   level == kCulled(-1) → 剔除（不进任何桶）
//   level == 0          → 高模桶
//   level >= 1          → 低模桶
// 本用例只验证「选档结果 → 桶」这一映射的正确性，不触碰 Vulkan/窗口/相机对象。
#include "framework/test_common.h"
#include "render/LodGroup.h"
#include "scene/CubeMesh.h"
#include "scene/EcsScene.h"
#include "scene/Scene.h"

using namespace BigHero;
using BigHero::Render::LodGroup;

namespace
{
// FOV 60° → 垂直半视角 30°，tan(30°) ≈ 0.577350269。
constexpr float kTanHalfFovY = 0.577350269f;

// 三档标准配置：h0=0.6 / h1=0.3 / h2=0.1，硬切换(fade=0)，bias=1，maxLevel=-1，剔除开启。
LodGroup MakeThreeLevels()
{
    LodGroup g;
    g.AddLevel(0.6f, 0.0f);
    g.AddLevel(0.3f, 0.0f);
    g.AddLevel(0.1f, 0.0f);
    return g;
}

// 渲染桶：与生产分桶一一对应。
enum class Bucket
{
    HighPoly, // level 0
    LowPoly,  // level >= 1
    Culled    // level == kCulled
};

// 选档 → 分桶（逻辑与生产代码一致）。
Bucket ClassifyBucket(const LodGroup& g, float worldRadius, float distance, float tanHalfFovY)
{
    const float screenH = LodGroup::ScreenRelativeHeight(worldRadius, distance, tanHalfFovY);
    const int level = g.SelectLevel(screenH);
    if (level == LodGroup::kCulled)
        return Bucket::Culled;
    return (level == 0) ? Bucket::HighPoly : Bucket::LowPoly;
}

// 只取原始档位，供额外断言具体 level 值。
int SelectLevelAt(const LodGroup& g, float worldRadius, float distance, float tanHalfFovY)
{
    return g.SelectLevel(LodGroup::ScreenRelativeHeight(worldRadius, distance, tanHalfFovY));
}
} // namespace

TEST_CASE("LodWiring.SelectLevelBucketMapping")
{
    LodGroup g = MakeThreeLevels();
    CHECK_NEAR(g.Bias(), 1.0f, 1e-6f); // 默认 bias=1
    CHECK(g.CullBeyondLast());         // 默认剔除开启

    const float R = Scene::kSphereBoundingRadius; // 0.5

    // 近距离 d=1.0：screenH = 0.5/(1.0*tan30°) ≈ 0.866 ≥ 0.6 → level 0 → 高模桶。
    CHECK_EQ(ClassifyBucket(g, R, 1.0f, kTanHalfFovY), Bucket::HighPoly);
    CHECK_EQ(SelectLevelAt(g, R, 1.0f, kTanHalfFovY), 0);

    // 中距离 d=2.0：screenH ≈ 0.433 ∈ [0.3,0.6) → level 1 → 低模桶。
    // （任务示例的 3.0m 实际 screenH≈0.289，落在 level2 区间；取 2.0m 稳定命中 level1。）
    CHECK_EQ(ClassifyBucket(g, R, 2.0f, kTanHalfFovY), Bucket::LowPoly);
    CHECK_EQ(SelectLevelAt(g, R, 2.0f, kTanHalfFovY), 1);

    // 远距离 d=10.0：screenH ≈ 0.087 < 0.1 → 剔除。
    CHECK_EQ(ClassifyBucket(g, R, 10.0f, kTanHalfFovY), Bucket::Culled);
}

TEST_CASE("LodWiring.EcsEntityBucketSplit")
{
    // 相机置于原点、沿 +Z 看；近/中/远三个球实体分别放在 z=1/2/10。
    Scene::EcsScene world;
    const float camDistances[3] = {1.0f, 2.0f, 10.0f};
    for (float d : camDistances)
    {
        Scene::SceneObject obj;
        obj.position = glm::vec3(0.0f, 0.0f, d);
        obj.scale = 1.0f;
        obj.tint = glm::vec3(1.0f);
        obj.spinSpeed = 0.0f;
        obj.phase = 0.0f;
        obj.meshId = 3; // 球
        obj.metallic = 0.0f;
        obj.roughness = 0.5f;
        (void)world.CreateObject(obj);
    }
    REQUIRE(world.ObjectCount() == 3);

    const LodGroup g = MakeThreeLevels();
    const glm::vec3 camPos(0.0f);
    int highPolyCount = 0, lowPolyCount = 0, culledCount = 0;

    // 模拟生产遍历：球心取世界矩阵平移列、半径=scale*boundsRadius，按选档结果分桶。
    world.ForEachRenderableWorld(
        [&](const Scene::ecs::Transform&, const Scene::ecs::Renderable& r, const Scene::ecs::Spin&,
            const glm::mat4& worldMat)
        {
            const glm::vec3 center = glm::vec3(worldMat[3]); // 无父实体：== position
            const float boundsRadius = (r.meshId == 3) ? Scene::kSphereBoundingRadius : 0.0f;
            const float radius = boundsRadius; // scale=1.0
            const float distance = glm::length(center - camPos);
            const float screenH = LodGroup::ScreenRelativeHeight(radius, distance, kTanHalfFovY);
            const int level = g.SelectLevel(screenH);
            if (level == LodGroup::kCulled)
                ++culledCount;
            else if (level == 0)
                ++highPolyCount;
            else
                ++lowPolyCount;
        });

    // 近/中/远各落一桶，ECS 集成层面分桶正确。
    CHECK_EQ(highPolyCount, 1);
    CHECK_EQ(lowPolyCount, 1);
    CHECK_EQ(culledCount, 1);
}

TEST_CASE("LodWiring.CapsuleSameLogic")
{
    const LodGroup g = MakeThreeLevels();
    const float R = Scene::kCapsuleBoundingRadius; // 1.0（比球大一圈）

    // 胶囊与球走同一套选档逻辑，仅包围半径不同。
    // 近距离 d=1.0：screenH = 1.0/(1.0*tan30°) ≈ 1.73 ≥ 0.6 → level 0 → 高模桶。
    CHECK_EQ(ClassifyBucket(g, R, 1.0f, kTanHalfFovY), Bucket::HighPoly);
    CHECK_EQ(SelectLevelAt(g, R, 1.0f, kTanHalfFovY), 0);

    // 远距离 d=25.0：screenH ≈ 0.069 < 0.1 → 剔除（半径大，需更远才剔）。
    CHECK_EQ(ClassifyBucket(g, R, 25.0f, kTanHalfFovY), Bucket::Culled);
}

TEST_CASE("LodWiring.BiasAffectsSelection")
{
    LodGroup g = MakeThreeLevels();
    const float R = Scene::kSphereBoundingRadius;
    const float dMid = 2.0f; // 无 bias 时 screenH≈0.433 → level 1（低模桶）

    // 偏向精细：bias=2.0 → 等效 screenH×2≈0.866 ≥ 0.6 → 升回高模桶。
    g.SetBias(2.0f);
    CHECK_EQ(ClassifyBucket(g, R, dMid, kTanHalfFovY), Bucket::HighPoly);

    // 偏向粗糙：bias=0.5 → 等效 screenH×0.5≈0.217 ∈ [0.1,0.3) → 低模桶（更粗的一档）。
    g.SetBias(0.5f);
    CHECK_EQ(ClassifyBucket(g, R, dMid, kTanHalfFovY), Bucket::LowPoly);

    g.SetBias(1.0f); // 复原，避免影响默认假设
}

TEST_CASE("LodWiring.CullBeyondLastToggle")
{
    LodGroup g = MakeThreeLevels();
    const float R = Scene::kSphereBoundingRadius;
    const float dFar = 10.0f; // screenH≈0.087 < 0.1

    // 默认开启剔除：远距离 → 剔除。
    CHECK(g.CullBeyondLast());
    CHECK_EQ(ClassifyBucket(g, R, dFar, kTanHalfFovY), Bucket::Culled);

    // 关闭剔除：钳到末档（level 2）→ 仍进低模桶，不剔除。
    g.SetCullBeyondLast(false);
    CHECK(!g.CullBeyondLast());
    CHECK_EQ(ClassifyBucket(g, R, dFar, kTanHalfFovY), Bucket::LowPoly);
    CHECK_EQ(SelectLevelAt(g, R, dFar, kTanHalfFovY), 2);
}
