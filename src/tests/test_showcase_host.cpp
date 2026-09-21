// 展示厅运行时子系统（ShowcaseHost）单元测试：注视解算 / 连续昼夜 / 画面风格 / 已体验打点。
// 纯逻辑（不持 GPU 资源），可离线运行；场景数据取自 samples/showcase/CyberCity。
#include "app/systems/ShowcaseHost.h"
#include "framework/test_common.h"
#include "showcase/CyberCity.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace BigHero;

namespace
{
// 站在展台正前方 6 m、眼高 1.6 m 处，视线指向展台悬浮展示物（高度 3 m）
struct EyeAt
{
    glm::vec3 eye;
    glm::vec3 forward;
};

[[nodiscard]] EyeAt StandBefore(const glm::vec3& exhibitPos, float focusHeight)
{
    const glm::vec3 eye = exhibitPos + glm::vec3(0.0f, 1.6f, 6.0f);
    const glm::vec3 target = exhibitPos + glm::vec3(0.0f, focusHeight, 0.0f);
    return {eye, glm::normalize(target - eye)};
}
} // namespace

TEST_CASE("ShowcaseHost.Focus")
{
    using namespace BigHero::Sample::Showcase;

    const CyberCityBuild b = BuildCyberCity();
    ShowcaseHost host;
    CHECK(!host.Active());
    host.Load(b);
    CHECK(host.Active());
    CHECK(host.Exhibits().size() == 8);

    // 站在 0 号展台前：应锁定 0 号
    const CyberExhibit& e0 = host.Exhibits()[0];
    const EyeAt a = StandBefore(e0.position, host.exhibitFocusHeight);
    host.Update(a.eye, a.forward);
    CHECK(host.FocusedIndex() == 0);
    CHECK(host.Focused() != nullptr);
    CHECK(std::strcmp(host.Focused()->name, e0.name) == 0);
    CHECK(host.FocusDistance() > 4.0f && host.FocusDistance() < 9.0f);

    // 背对展台：不应锁定
    host.Update(a.eye, -a.forward);
    CHECK(host.FocusedIndex() == -1);
    CHECK(host.Focused() == nullptr);

    // 站得太远：超出 focusMaxDistance 后失效
    const glm::vec3 far = e0.position + glm::vec3(0.0f, 1.6f, 60.0f);
    const glm::vec3 farTarget = e0.position + glm::vec3(0.0f, host.exhibitFocusHeight, 0.0f);
    host.Update(far, glm::normalize(farTarget - far));
    CHECK(host.FocusedIndex() == -1);

    // 数字键直达：1..8 为八座展台（物理 9 号走物理游乐区，不设展台）
    for (int f = 1; f <= 8; ++f)
    {
        const CyberExhibit* ex = host.FindByFeature(f);
        CHECK(ex != nullptr);
        if (ex != nullptr)
            CHECK(ex->featureId == f);
    }
    CHECK(host.FindByFeature(9) == nullptr);
    CHECK(host.FindByFeature(99) == nullptr);
}

TEST_CASE("ShowcaseHost.DayCycle")
{
    using namespace BigHero::Sample::Showcase;

    const CyberCityBuild b = BuildCyberCity();
    ShowcaseHost host;
    host.Load(b);

    const int n = static_cast<int>(CyberCityTimePresets().size());
    CHECK(n == 4);
    CHECK(host.TimeOfDayIndex() == 1); // 默认夜晚

    // 静止（dt=0）不应自行漂移
    host.AdvanceTime(0.0f);
    CHECK(std::fabs(host.DayTime() - 1.0f) < 1e-6f);

    // 切到下一时段：需若干秒平滑收敛，且不是瞬间跳变
    host.CycleTimeOfDay();
    CHECK(host.TimeOfDayIndex() == 2);
    host.AdvanceTime(0.1f);
    const float after01 = host.DayTime();
    CHECK(after01 > 1.0f && after01 < 2.0f); // 已启动但尚未到位
    for (int i = 0; i < 300; ++i)
        host.AdvanceTime(1.0f / 60.0f);
    CHECK(std::fabs(host.DayTime() - 2.0f) < 1e-3f);

    // 锚点处的插值结果应等于该预设
    const CyberTimePreset blended = host.BlendedPreset();
    const CyberTimePreset& target = CyberCityTimePresets()[2];
    CHECK(std::fabs(blended.sunIntensity - target.sunIntensity) < 1e-3f);
    CHECK(std::fabs(blended.exposure - target.exposure) < 1e-3f);

    // 环上最短路径：从 0 号切回上一个应倒退到 3 号（而不是穿过 1、2）
    host.SetTimeOfDay(0);
    for (int i = 0; i < 200; ++i)
        host.AdvanceTime(1.0f / 60.0f);
    CHECK(host.TimeOfDayIndex() == 0);
    host.SetTimeOfDay(3);
    host.AdvanceTime(0.2f);
    CHECK(host.DayTime() < 0.0f || host.DayTime() > 3.0f); // 已越过 0 向 -1（=3）方向倒退

    // 自动循环：绕环一圈后回到起点附近，且始终在合法区间
    host.SetTimeOfDay(0);
    host.SetAutoCycle(true);
    float minV = 1e9f;
    float maxV = -1e9f;
    for (int i = 0; i < 18 * 4 * 6; ++i) // 约 4 个预设 × 18 秒
    {
        host.AdvanceTime(1.0f / 6.0f);
        const float d = host.DayTime();
        CHECK(d >= 0.0f && d < static_cast<float>(n));
        minV = std::min(minV, d);
        maxV = std::max(maxV, d);
    }
    CHECK(minV < 0.5f);
    CHECK(maxV > 3.0f); // 走满整圈
}

TEST_CASE("ShowcaseHost.LookGallery")
{
    using namespace BigHero::Sample::Showcase;

    const CyberCityBuild b = BuildCyberCity();
    ShowcaseHost host;
    host.Load(b);

    CHECK(host.LookIndex() == 0);
    CHECK(std::strcmp(host.Look().name, CyberCityLookPresets()[0].name) == 0);

    const int n = static_cast<int>(CyberCityLookPresets().size());
    for (int i = 1; i <= n; ++i)
    {
        host.CycleLook();
        CHECK(host.LookIndex() == i % n);
    }
    CHECK(host.LookIndex() == 0); // 绕回

    host.SetLook(2);
    CHECK(host.LookIndex() == 2);
    CHECK(host.Look().gradeSaturation < 1.0f); // 冷冽清晨：低饱和
    host.SetLook(-1);                          // 负索引回绕到末尾
    CHECK(host.LookIndex() == n - 1);
    CHECK(host.LookIndex() >= 0 && host.LookIndex() < n);
}

TEST_CASE("ShowcaseHost.VisitedTracking")
{
    using namespace BigHero::Sample::Showcase;

    const CyberCityBuild b = BuildCyberCity();
    ShowcaseHost host;
    host.Load(b);

    CHECK(host.VisitedCount() == 0);
    for (int f = 1; f <= 9; ++f)
    {
        CHECK(!host.Visited(f));
        host.MarkFeature(f);
        CHECK(host.Visited(f));
    }
    CHECK(host.VisitedCount() == 9);

    // 重复打点不叠加
    host.MarkFeature(1);
    CHECK(host.VisitedCount() == 9);

    // 越界 id 不参与位图（不越界写内存）
    host.MarkFeature(0);
    host.MarkFeature(32);
    CHECK(!host.Visited(0));
    CHECK(!host.Visited(32));
    CHECK(host.VisitedCount() == 9);

    host.Reset();
    CHECK(host.VisitedCount() == 0);
    CHECK(host.TimeOfDayIndex() == 1);
    CHECK(host.LookIndex() == 0);
}
