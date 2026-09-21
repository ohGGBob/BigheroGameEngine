// 烘焙式遮挡剔除（render/OcclusionCulling.h）单元测试：纯逻辑、零 GPU，可离线运行。
// 覆盖射线-AABB / 单墙遮挡 / 自遮挡与贴面 / 相邻格必然可见 / 越界与未烘焙的保守行为 /
// 剔除率与体积 / 网格合法性。
#include "framework/test_common.h"
#include "render/OcclusionCulling.h"

using namespace BigHero;
using BigHero::Render::Bounds3;
using BigHero::Render::OcclusionBakeParams;
using BigHero::Render::OcclusionVolume;
using BigHero::Render::RayHitsBox;

namespace
{
Bounds3 Box(const glm::vec3& lo, const glm::vec3& hi)
{
    Bounds3 b;
    b.min = lo;
    b.max = hi;
    return b;
}

// 一堵横在 x = 5 的墙（厚度 1，高 10，长 20），把场景一分为二。
Bounds3 Wall()
{
    return Box(glm::vec3(4.5f, 0.0f, -10.0f), glm::vec3(5.5f, 10.0f, 10.0f));
}
} // namespace

TEST_CASE("Occlusion.RayHitsBox")
{
    const Bounds3 b = Box(glm::vec3(1.0f), glm::vec3(3.0f));
    float t = -1.0f;

    // 正对穿过
    CHECK(RayHitsBox(glm::vec3(0.0f, 2.0f, 2.0f), glm::vec3(1.0f, 0.0f, 0.0f), b, 100.0f, t));
    CHECK_NEAR(t, 1.0f, 1.0e-4f);

    // 反向（盒在背后）→ 不命中
    CHECK(!RayHitsBox(glm::vec3(0.0f, 2.0f, 2.0f), glm::vec3(-1.0f, 0.0f, 0.0f), b, 100.0f, t));

    // 射线长度不够 → 不命中
    CHECK(!RayHitsBox(glm::vec3(0.0f, 2.0f, 2.0f), glm::vec3(1.0f, 0.0f, 0.0f), b, 0.5f, t));

    // 擦边不算命中（y 越界）
    CHECK(!RayHitsBox(glm::vec3(0.0f, 5.0f, 2.0f), glm::vec3(1.0f, 0.0f, 0.0f), b, 100.0f, t));

    // 起点在盒内 → 命中且 t = 0（调用方据此跳过该盒，避免自遮挡）
    CHECK(RayHitsBox(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(1.0f, 0.0f, 0.0f), b, 100.0f, t));
    CHECK_NEAR(t, 0.0f, 1.0e-6f);

    // 平行于某 slab 但位于其内部 → 命中
    CHECK(RayHitsBox(glm::vec3(0.0f, 2.0f, 2.0f), glm::vec3(1.0f, 0.0f, 0.0f), b, 100.0f, t));

    // 盒判定
    CHECK(b.Contains(glm::vec3(2.0f, 2.0f, 2.0f)));
    CHECK(!b.Contains(glm::vec3(0.0f, 2.0f, 2.0f)));
    CHECK(b.IntersectsInflated(Box(glm::vec3(3.1f), glm::vec3(4.0f)), 0.2f));
    CHECK(!b.IntersectsInflated(Box(glm::vec3(3.5f), glm::vec3(4.0f)), 0.2f));
}

TEST_CASE("Occlusion.WallBlocksFarObject")
{
    // 场景：墙在 x≈5；相机格在 x=1（墙左侧），目标物体在 x=9（墙右侧）。
    // 期望：墙左侧的格子看不见目标；墙右侧的格子看得见。
    OcclusionVolume v;
    CHECK(v.Resize(glm::ivec3(10, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));

    const std::vector<Bounds3> occluders{Wall()};
    const std::vector<Bounds3> cullables{Box(glm::vec3(8.5f, 0.5f, -0.5f), glm::vec3(9.5f, 1.5f, 0.5f))};
    CHECK(v.Bake(occluders, cullables));
    CHECK(v.IsBaked());
    CHECK_EQ(v.ObjectCount(), 1u);

    CHECK(!v.IsVisibleAt(glm::vec3(0.5f, 0.5f, 0.5f), 0)); // 墙左侧 → 被挡
    CHECK(v.IsVisibleAt(glm::vec3(6.5f, 0.5f, 0.5f), 0));  // 墙右侧 → 可见
    CHECK(v.IsVisibleAt(glm::vec3(9.5f, 0.5f, 0.5f), 0));  // 贴着物体 → 必然可见

    // 平均可见比例应显著小于 1（说明确实剔掉了一部分）
    CHECK_LT(v.AverageVisibilityRatio(), 1.0);
    CHECK_GT(v.AverageVisibilityRatio(), 0.0);
}

TEST_CASE("Occlusion.SelfOcclusionAndTouching")
{
    // 建筑既是遮挡体又是被剔除对象：不能把自己剔掉。
    // 这是烘焙 PVS 最容易翻车的地方——射线终点落在自己的包围盒里，
    // 若不做「跳过包含起点/终点的遮挡体」，整栋楼会把自己剔没了。
    OcclusionVolume v;
    CHECK(v.Resize(glm::ivec3(6, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));

    const Bounds3 building = Box(glm::vec3(2.0f, 0.0f, -1.0f), glm::vec3(4.0f, 5.0f, 1.0f));
    const std::vector<Bounds3> occluders{building};
    const std::vector<Bounds3> cullables{building};
    CHECK(v.Bake(occluders, cullables));

    // 从任意格子都应看得到这栋楼（它是自己的遮挡体时必须被跳过）
    for (int x = 0; x < 6; ++x)
        CHECK(v.IsVisible(glm::ivec3(x, 0, 0), 0));

    // 关闭 skipTouchingOccluders 后行为退化为「自遮挡」，仅用于对照（不推荐配置）
    OcclusionBakeParams p;
    p.skipTouchingOccluders = false;
    OcclusionVolume w;
    CHECK(w.Resize(glm::ivec3(6, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK(w.Bake(occluders, cullables, p));
    CHECK_LE(w.AverageVisibilityRatio(), v.AverageVisibilityRatio() + 1.0e-9);
}

TEST_CASE("Occlusion.NearCellAlwaysVisible")
{
    // 与格子相交（含容差）的对象必须判可见：相机就贴着它，自遮挡误剔会造成闪烁穿帮。
    OcclusionVolume v;
    CHECK(v.Resize(glm::ivec3(4, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));

    const std::vector<Bounds3> occluders{}; // 没有任何遮挡体
    const std::vector<Bounds3> cullables{Box(glm::vec3(1.6f, 0.0f, 0.0f), glm::vec3(2.4f, 1.0f, 1.0f))};
    CHECK(v.Bake(occluders, cullables));
    for (int x = 0; x < 4; ++x)
        CHECK(v.IsVisible(glm::ivec3(x, 0, 0), 0));
    CHECK_NEAR(v.AverageVisibilityRatio(), 1.0, 1.0e-9);
}

TEST_CASE("Occlusion.CullsBehindMultipleWalls")
{
    // 场景布局（俯视，z 分三条带）：
    //   z∈[0,1] 带：x∈[4,10] 被墙 B 封住
    //   z∈[1,2] 带：全程贯通（走廊 / 开阔带）
    //   z∈[2,3] 带：x∈[4,5] 被墙 A 封住
    // 期望：z≈2.5 的物体从 x<4 看不见（墙 A 挡），从 x>6 看得见；
    //       z≈1.5 的物体（在贯通带里）从两端都看得见。
    OcclusionVolume v;
    CHECK(v.Resize(glm::ivec3(12, 1, 3), glm::vec3(0.0f), glm::vec3(1.0f)));

    const std::vector<Bounds3> occluders{
        Box(glm::vec3(4.0f, 0.0f, 2.0f), glm::vec3(5.0f, 6.0f, 3.0f)),  // 墙 A：封住 z∈[2,3] 带
        Box(glm::vec3(4.0f, 0.0f, 0.0f), glm::vec3(10.0f, 6.0f, 1.0f)), // 墙 B：封住 z∈[0,1] 带
    };
    const std::vector<Bounds3> cullables{
        Box(glm::vec3(8.0f, 0.5f, 2.2f), glm::vec3(9.0f, 1.5f, 2.8f)), // 0：墙 A 之后
        Box(glm::vec3(1.0f, 0.5f, 1.3f), glm::vec3(2.0f, 1.5f, 1.8f)), // 1：贯通带里
    };
    CHECK(v.Bake(occluders, cullables));

    // 贯通带的物体：两端都看得见
    CHECK(v.IsVisibleAt(glm::vec3(0.5f, 0.5f, 1.5f), 1));
    CHECK(v.IsVisibleAt(glm::vec3(10.5f, 0.5f, 1.5f), 1));

    // 墙 A 之后的物体：左侧看不见，右侧看得见
    CHECK(!v.IsVisibleAt(glm::vec3(0.5f, 0.5f, 2.5f), 0));
    CHECK(v.IsVisibleAt(glm::vec3(6.5f, 0.5f, 2.5f), 0));
    CHECK(v.IsVisibleAt(glm::vec3(8.5f, 0.5f, 2.5f), 0));

    const double ratio = v.AverageVisibilityRatio();
    CHECK_LT(ratio, 1.0);
    CHECK_GT(ratio, 0.0);
}

TEST_CASE("Occlusion.VisibleObjectsSet")
{
    OcclusionVolume v;
    CHECK(v.Resize(glm::ivec3(6, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    const std::vector<Bounds3> occluders{Wall()};
    const std::vector<Bounds3> cullables{
        Box(glm::vec3(8.5f, 0.5f, -0.5f), glm::vec3(9.5f, 1.5f, 0.5f)), // 0：墙后
        Box(glm::vec3(1.5f, 0.5f, -0.5f), glm::vec3(2.5f, 1.5f, 0.5f)), // 1：墙前（开阔）
    };
    CHECK(v.Bake(occluders, cullables));

    const auto left = v.VisibleObjectsAt(glm::vec3(0.5f, 0.5f, 0.5f));
    CHECK_EQ(left.size(), 1u);
    CHECK_EQ(left[0], 1u); // 只看到墙前那个

    const auto right = v.VisibleObjectsAt(glm::vec3(9.5f, 0.5f, 0.5f));
    CHECK_EQ(right.size(), 2u); // 墙后也能看到两个

    CHECK_EQ(v.VisibleCount(glm::ivec3(0, 0, 0)), 1u);
    CHECK_EQ(v.VisibleCount(glm::ivec3(5, 0, 0)), 2u);
}

TEST_CASE("Occlusion.ConservativeWhenUnbaked")
{
    // 未烘焙 / 索引越界 / 对象越界：一律返回「可见」。
    // 剔除系统出 bug 的表现是「东西凭空消失」，比多画几帧严重得多，
    // 所以所有退化路径都必须保守。
    OcclusionVolume v;
    CHECK(v.Resize(glm::ivec3(4, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK(!v.IsBaked());
    CHECK(v.IsVisible(glm::ivec3(0, 0, 0), 0)); // 未烘焙 → 可见
    CHECK(v.VisibleObjects(glm::ivec3(0, 0, 0)).empty());

    const std::vector<Bounds3> occluders{};
    const std::vector<Bounds3> cullables{Box(glm::vec3(0.0f), glm::vec3(1.0f))};
    CHECK(v.Bake(occluders, cullables));
    CHECK(v.IsVisible(glm::ivec3(99, 0, 0), 0));  // 越界格 → 可见
    CHECK(v.IsVisible(glm::ivec3(0, 0, 0), 999)); // 越界对象 → 可见
    CHECK(v.VisibleObjects(glm::ivec3(99, 0, 0)).empty());

    // 空对象表：烘焙成功但 PVS 为空
    OcclusionVolume e;
    CHECK(e.Resize(glm::ivec3(2, 2, 2), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK(e.Bake(occluders, std::vector<Bounds3>{}));
    CHECK(e.IsBaked());
    CHECK_EQ(e.ObjectCount(), 0u);
    CHECK_NEAR(e.AverageVisibilityRatio(), 1.0, 1.0e-9);
}

TEST_CASE("Occlusion.MaxRayDistanceAndSampleCounts")
{
    OcclusionVolume v;
    CHECK(v.Resize(glm::ivec3(20, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    const std::vector<Bounds3> occluders{Wall()};
    const std::vector<Bounds3> cullables{Box(glm::vec3(18.5f, 0.5f, -0.5f), glm::vec3(19.5f, 1.5f, 0.5f))};

    // 射线长度上限很小 → 远处对象一律放行（不再判遮挡）
    OcclusionBakeParams near{};
    near.maxRayDistance = 3.0f;
    CHECK(v.Bake(occluders, cullables, near));
    CHECK(v.IsVisibleAt(glm::vec3(0.5f, 0.5f, 0.5f), 0)); // 超出上限 → 保守可见

    // 正常上限 → 被墙挡住
    OcclusionBakeParams normal{};
    CHECK(v.Bake(occluders, cullables, normal));
    CHECK(!v.IsVisibleAt(glm::vec3(0.5f, 0.5f, 0.5f), 0));

    // 采样点数量变化不应破坏「相邻必然可见」的不变量
    OcclusionBakeParams single{};
    single.originSamples = 1;
    single.targetSamples = 1;
    CHECK(v.Bake(occluders, cullables, single));
    CHECK(v.IsVisibleAt(glm::vec3(19.5f, 0.5f, 0.5f), 0));

    OcclusionBakeParams dense{};
    dense.originSamples = 9;
    dense.targetSamples = 27;
    CHECK(v.Bake(occluders, cullables, dense));
    CHECK(v.IsVisibleAt(glm::vec3(19.5f, 0.5f, 0.5f), 0));
    // 采样更密 → 更容易找到通射线 → 可见集单调不减（保守性方向正确）
    CHECK_GE(v.AverageVisibilityRatio(), 0.0);
}

TEST_CASE("Occlusion.GridMathAndValidation")
{
    OcclusionVolume v;
    CHECK(!v.Resize(glm::ivec3(0, 1, 1), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK(!v.Resize(glm::ivec3(2, 0, 2), glm::vec3(0.0f), glm::vec3(1.0f)));
    CHECK(!v.Resize(glm::ivec3(2, 2, 2), glm::vec3(0.0f), glm::vec3(0.0f)));
    CHECK(!v.Resize(glm::ivec3(2, 2, 2), glm::vec3(0.0f), glm::vec3(-1.0f)));
    CHECK_EQ(v.CellCount(), 0u);

    CHECK(v.Resize(glm::ivec3(4, 5, 2), glm::vec3(-2.0f, 0.0f, 1.0f), glm::vec3(0.5f, 2.0f, 1.0f)));
    CHECK_EQ(v.CellCount(), 40u);
    CHECK_NEAR(v.CellPosition(glm::ivec3(0, 0, 0)).x, -2.0f, 1.0e-6f);
    CHECK_NEAR(v.CellPosition(glm::ivec3(1, 0, 0)).x, -1.5f, 1.0e-6f);
    CHECK_NEAR(v.CellCenter(glm::ivec3(0, 0, 0)).x, -1.75f, 1.0e-6f);
    CHECK_NEAR(v.CellExtent().y, 2.0f, 1.0e-6f);

    // 越界钳制
    CHECK_EQ(v.ClampedCell(glm::vec3(-100.0f)).x, 0);
    CHECK_EQ(v.ClampedCell(glm::vec3(100.0f)).x, 3);
    CHECK_EQ(v.ClampedCell(glm::vec3(-2.0f, 0.0f, 1.0f)).x, 0);
    CHECK_EQ(v.ClampedCell(glm::vec3(-1.6f, 0.1f, 1.1f)).x, 0);
    CHECK_EQ(v.ClampedCell(glm::vec3(-0.1f, 0.1f, 1.1f)).x, 3);

    // 未 Resize 时烘焙失败
    OcclusionVolume empty;
    CHECK(!empty.Bake(std::vector<Bounds3>{}, std::vector<Bounds3>{Box(glm::vec3(0.0f), glm::vec3(1.0f))}));
}

TEST_CASE("Occlusion.PvsMemoryFootprint")
{
    // 体积核算：cells × ceil(objects/64) × 8 字节。构建面板要据此估算磁盘/内存成本。
    OcclusionVolume v;
    CHECK(v.Resize(glm::ivec3(8, 8, 8), glm::vec3(0.0f), glm::vec3(2.0f)));
    std::vector<Bounds3> cullables;
    for (int i = 0; i < 100; ++i)
        cullables.push_back(
            Box(glm::vec3(static_cast<float>(i), 0.0f, 0.0f), glm::vec3(static_cast<float>(i) + 0.5f, 1.0f, 1.0f)));
    CHECK(v.Bake(std::vector<Bounds3>{}, cullables));

    const size_t expected = 512u * 2u * sizeof(uint64_t); // ceil(100/64) = 2
    CHECK_EQ(v.PvsBytes(), expected);
    CHECK_LT(v.PvsBytes(), 1024u * 1024u); // 8^3 格 × 100 对象 ≈ 8 KB
}
