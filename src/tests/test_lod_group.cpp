// LOD Group（render/LodGroup.h）单元测试：纯逻辑、零 GPU，可离线运行。
// 覆盖阈值语义 / 剔除 / 交叉渐变权重 / Bias 与 MaxLevel 钳制 / 屏幕相对高度换算 / 档位合法性。
#include "framework/test_common.h"
#include "render/LodGroup.h"

using namespace BigHero;
using BigHero::Render::LodGroup;
using BigHero::Render::LodLevel;

namespace
{
// 三档标准配置：LOD0 阈值 0.6、LOD1 阈值 0.3、LOD2 阈值 0.1（与 Unity 面板默认形态一致）。
LodGroup MakeThreeLevels()
{
    LodGroup g;
    CHECK(g.AddLevel(0.6f));
    CHECK(g.AddLevel(0.3f));
    CHECK(g.AddLevel(0.1f));
    return g;
}
} // namespace

TEST_CASE("Lod.ThresholdSemantics")
{
    const LodGroup g = MakeThreeLevels();
    CHECK_EQ(g.Count(), 3u);

    // 阈值是「本档使用下界」：r >= h_0 → 档 0；h_i <= r < h_(i-1) → 档 i。
    CHECK_EQ(g.SelectLevel(1.0f), 0);
    CHECK_EQ(g.SelectLevel(0.6f), 0); // 下界闭区间
    CHECK_EQ(g.SelectLevel(0.59f), 1);
    CHECK_EQ(g.SelectLevel(0.3f), 1);
    CHECK_EQ(g.SelectLevel(0.29f), 2);
    CHECK_EQ(g.SelectLevel(0.1f), 2);
    // 低于末档阈值 → 剔除
    CHECK_EQ(g.SelectLevel(0.099f), LodGroup::kCulled);
    CHECK_EQ(g.SelectLevel(0.0f), LodGroup::kCulled);
}

TEST_CASE("Lod.CullToggleAndEmpty")
{
    LodGroup g = MakeThreeLevels();
    CHECK(g.CullBeyondLast()); // 默认剔除
    CHECK_EQ(g.SelectLevel(0.05f), LodGroup::kCulled);

    g.SetCullBeyondLast(false);
    CHECK(!g.CullBeyondLast());
    CHECK_EQ(g.SelectLevel(0.05f), 2); // 强制最粗档

    LodGroup empty;
    CHECK_EQ(empty.Count(), 0u);
    CHECK_EQ(empty.SelectLevel(1.0f), LodGroup::kCulled);
    const auto sel = empty.Evaluate(1.0f);
    CHECK(sel.IsCulled());
    CHECK(!sel.IsBlending());
}

TEST_CASE("Lod.CrossFadeWeights")
{
    LodGroup g;
    // 两档 + 50% 渐变宽度：档 0 区间 [0.5, 1.0]，底部 50% 用于混合 → fadeStart = 0.75
    CHECK(g.AddLevel(0.5f, 0.5f));
    CHECK(g.AddLevel(0.1f, 0.0f));

    const auto pure = g.Evaluate(0.9f);
    CHECK_EQ(pure.level, 0);
    CHECK(!pure.IsBlending());
    CHECK_NEAR(pure.fade, 1.0f, 1e-6f);

    const auto edge = g.Evaluate(0.75f);
    CHECK_EQ(edge.level, 0);
    CHECK(!edge.IsBlending()); // fadeStart 处仍是纯档（闭区间）
    CHECK_NEAR(edge.fade, 1.0f, 1e-6f);

    const auto mid = g.Evaluate(0.625f); // (0.625-0.5)/(0.75-0.5) = 0.5
    CHECK_EQ(mid.level, 0);
    CHECK(mid.IsBlending());
    CHECK_EQ(mid.fadeLevel, 1);
    CHECK_NEAR(mid.fade, 0.5f, 1e-5f);
    CHECK_NEAR(1.0f - mid.fade, 0.5f, 1e-5f); // 次档权重互补

    const auto atFloor = g.Evaluate(0.5f); // 恰好在阈值上 → 权重 0，全归次档
    CHECK_EQ(atFloor.level, 0);
    CHECK(atFloor.IsBlending());
    CHECK_NEAR(atFloor.fade, 0.0f, 1e-5f);

    // 权重随距离单调：越远（r 越小）越偏向粗档。
    float prev = 1.0f;
    for (float r = 0.749f; r >= 0.5001f; r -= 0.01f)
    {
        const auto s = g.Evaluate(r);
        CHECK(s.IsBlending());
        CHECK_LE(s.fade, prev + 1e-6f);
        prev = s.fade;
    }
}

TEST_CASE("Lod.NoFadeIsHardSwitch")
{
    LodGroup g;
    CHECK(g.AddLevel(0.5f, 0.0f)); // 显式 0 宽度
    CHECK(g.AddLevel(0.1f, 0.0f));

    const auto a = g.Evaluate(0.51f);
    CHECK(!a.IsBlending());
    const auto b = g.Evaluate(0.49f);
    CHECK(!b.IsBlending());
    CHECK_EQ(b.level, 1);

    // 末档无下一档可混，即便给了宽度也必须是纯档。
    LodGroup h;
    CHECK(h.AddLevel(0.5f, 1.0f));
    const auto last = h.Evaluate(0.6f);
    CHECK_EQ(last.level, 0);
    CHECK(!last.IsBlending());
}

TEST_CASE("Lod.BiasAndMaxLevel")
{
    LodGroup g = MakeThreeLevels();

    // Bias > 1：等效屏幕占比放大 → 更久地停留在精细档。
    g.SetBias(2.0f);
    CHECK_EQ(g.SelectLevel(0.3f), 0); // 0.3 * 2 = 0.6 → 档 0
    CHECK_EQ(g.SelectLevel(0.15f), 1);

    // Bias < 1：更激进地降档（性能优先）。
    g.SetBias(0.5f);
    CHECK_EQ(g.SelectLevel(0.9f), 1); // 0.9 * 0.5 = 0.45 → 档 1
    CHECK_EQ(g.SelectLevel(0.3f), 2); // 0.3 * 0.5 = 0.15 → 档 2（尚未到剔除线 0.1）
    CHECK_EQ(g.SelectLevel(0.1f), LodGroup::kCulled); // 0.1 * 0.5 = 0.05 → 剔除

    // Bias 钳制：极端值不至于把整组钉死或溢出。
    g.SetBias(0.0f);
    CHECK_GE(g.Bias(), 0.01f);
    g.SetBias(100.0f);
    CHECK_LE(g.Bias(), 10.0f);
    g.SetBias(1.0f);

    // MaxLevel：画质档位降级时跳过最精细档。
    g.SetMaxLevel(1);
    CHECK_EQ(g.SelectLevel(1.0f), 1);
    CHECK_EQ(g.SelectLevel(0.6f), 1);
    CHECK_EQ(g.SelectLevel(0.4f), 1);
    CHECK_EQ(g.SelectLevel(0.05f), LodGroup::kCulled);

    // MaxLevel 越界不产生非法索引。
    g.SetMaxLevel(99);
    CHECK_EQ(g.SelectLevel(1.0f), 2);
    CHECK_EQ(g.SelectLevel(0.05f), LodGroup::kCulled);
    g.SetMaxLevel(-1);
    CHECK_EQ(g.SelectLevel(1.0f), 0);
}

TEST_CASE("Lod.MaxLevelKeepsValidFade")
{
    LodGroup g;
    CHECK(g.AddLevel(0.5f, 0.5f)); // 档 0 区间 [0.5, 1.0]，fadeStart = 0.75
    CHECK(g.AddLevel(0.1f, 0.0f));

    g.SetMaxLevel(1);
    const auto s = g.Evaluate(0.6f);
    CHECK_EQ(s.level, 1);
    // 钳制后主档为 1，末档无下一档 → 必须是纯档，且不得出现同档自混合。
    CHECK(!s.IsBlending());
    CHECK_NEAR(s.fade, 1.0f, 1e-6f);
}

TEST_CASE("Lod.ScreenRelativeHeightMath")
{
    // 半径 1 的球，垂直半视角正切 1.0（fov 90°），距离 2 → 占屏 0.5。
    CHECK_NEAR(LodGroup::ScreenRelativeHeight(1.0f, 2.0f, 1.0f), 0.5f, 1e-5f);

    // 距离加倍 → 占屏减半。
    CHECK_NEAR(LodGroup::ScreenRelativeHeight(1.0f, 4.0f, 1.0f), 0.25f, 1e-5f);
    // 物体加倍 → 占屏加倍。
    CHECK_NEAR(LodGroup::ScreenRelativeHeight(2.0f, 2.0f, 1.0f), 1.0f, 1e-5f);
    // 长焦（tan 变小）→ 占屏变大 → 更久停在精细档。
    const float wide = LodGroup::ScreenRelativeHeight(1.0f, 10.0f, std::tan(1.04719755f)); // 60° 半角
    const float tele = LodGroup::ScreenRelativeHeight(1.0f, 10.0f, std::tan(0.17453293f)); // 10° 半角
    CHECK_GT(tele, wide);

    // 退化输入：距离/视角非正时钳制，不产生 NaN/Inf。
    const float degenerate = LodGroup::ScreenRelativeHeight(1.0f, 0.0f, 0.0f);
    CHECK(degenerate > 0.0f);
    CHECK(degenerate < 1e7f);

    // 反解自洽：距离 → 占屏 → 距离 应闭合。
    const float d = LodGroup::DistanceForScreenRelativeHeight(2.5f, 0.3f, 0.5f);
    CHECK_NEAR(LodGroup::ScreenRelativeHeight(2.5f, d, 0.5f), 0.3f, 1e-4f);

    // 阈值距离表：档位越粗 → 切换距离越远。
    const LodGroup g = MakeThreeLevels();
    const auto dists = g.DistanceThresholds(1.0f, 0.5f);
    CHECK_EQ(dists.size(), 3u);
    CHECK_LT(dists[0], dists[1]);
    CHECK_LT(dists[1], dists[2]);
}

TEST_CASE("Lod.LevelValidation")
{
    LodGroup g;
    // 阈值必须落在 (0, 1]
    CHECK(!g.AddLevel(0.0f));
    CHECK(!g.AddLevel(-0.1f));
    CHECK(!g.AddLevel(1.5f));
    CHECK_EQ(g.Count(), 0u);

    // 必须严格递减（相等/递增的档永远不可达）
    CHECK(g.AddLevel(0.5f));
    CHECK(!g.AddLevel(0.5f));
    CHECK(!g.AddLevel(0.7f));
    CHECK(g.AddLevel(0.2f));
    CHECK_EQ(g.Count(), 2u);

    // 档位数上限
    LodGroup full;
    float h = 0.9f;
    for (size_t i = 0; i < LodGroup::kMaxLevels; ++i)
    {
        CHECK(full.AddLevel(h));
        h *= 0.5f;
    }
    CHECK_EQ(full.Count(), LodGroup::kMaxLevels);
    CHECK(!full.AddLevel(0.001f));
}

TEST_CASE("Lod.SetLevelsSortsAndRejects")
{
    LodGroup g;
    std::vector<LodLevel> levels{{0.1f, 0.0f}, {0.6f, 0.2f}, {0.3f, 0.1f}};
    CHECK(g.SetLevels(levels));
    CHECK_EQ(g.Count(), 3u);
    // 自动降序排列
    CHECK_NEAR(g.Levels()[0].screenRelativeHeight, 0.6f, 1e-6f);
    CHECK_NEAR(g.Levels()[1].screenRelativeHeight, 0.3f, 1e-6f);
    CHECK_NEAR(g.Levels()[2].screenRelativeHeight, 0.1f, 1e-6f);
    CHECK_EQ(g.SelectLevel(1.0f), 0);
    CHECK_EQ(g.SelectLevel(0.05f), LodGroup::kCulled);

    // 重复阈值 → 区间退化，拒绝且不改动原状态
    std::vector<LodLevel> dup{{0.5f, 0.0f}, {0.5f, 0.0f}};
    CHECK(!g.SetLevels(dup));
    CHECK_EQ(g.Count(), 3u); // 原状态保留

    // 渐变宽度越界被钳制
    std::vector<LodLevel> wild{{0.6f, -5.0f}, {0.3f, 9.0f}};
    CHECK(g.SetLevels(wild));
    CHECK_GE(g.Levels()[0].fadeTransitionWidth, 0.0f);
    CHECK_LE(g.Levels()[1].fadeTransitionWidth, 1.0f);

    // 超上限 / 非法阈值
    std::vector<LodLevel> tooMany;
    for (size_t i = 0; i < LodGroup::kMaxLevels + 1; ++i)
        tooMany.push_back(LodLevel{1.0f / static_cast<float>(i + 1), 0.0f});
    CHECK(!g.SetLevels(tooMany));
}

TEST_CASE("Lod.MonotonicAcrossSweep")
{
    // 从贴脸到极远扫一遍，档位必须单调变粗（这是 LOD 正确性的核心不变量）。
    const LodGroup g = MakeThreeLevels();
    int prevIdx = 0;
    for (float d = 1.0f; d <= 200.0f; d *= 1.05f)
    {
        const float r = LodGroup::ScreenRelativeHeight(2.0f, d, 0.6f);
        const int idx = g.SelectLevel(r);
        if (idx == LodGroup::kCulled)
            break; // 之后恒为剔除
        CHECK_GE(idx, prevIdx);
        prevIdx = idx;
    }
    CHECK_EQ(prevIdx, 2);
}
