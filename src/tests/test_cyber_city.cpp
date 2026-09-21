// 赛博城市展示厅场景（samples/showcase/CyberCity）单元测试：纯 CPU、确定性，可离线运行。
// 覆盖规模规格 / 确定性 / 展台与霓虹布局 / 时段预设 / 出生点可站立（与 FP 控制器联调）。
#include "framework/test_common.h"
#include "game/FpController.h"
#include "showcase/CyberCity.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace BigHero;

TEST_CASE("CyberCity.Spec")
{
    using namespace BigHero::Sample::Showcase;

    const CyberCityBuild b = BuildCyberCity();
    CHECK(b.objects.size() >= 120);
    CHECK(b.objects.size() <= 800);  // 规模受控（帧预算友好）
    CHECK(b.colliders.size() >= 12); // 楼 / 灯柱 / 展台 / 雕塑 / 物理台
    CHECK(b.colliders.size() <= 120);
    CHECK(b.exhibits.size() == 8);
    CHECK(b.neons.size() == 8);
    CHECK(b.buildingCount == 10);
    CHECK(b.skylineCount == 24);

    // 自发光物体存在（霓虹 / 窗格 / 灯球）
    std::size_t emissiveCount = 0;
    for (const Scene::SceneObject& o : b.objects)
    {
        if (o.emissive.r > 0.01f || o.emissive.g > 0.01f || o.emissive.b > 0.01f)
            ++emissiveCount;
    }
    CHECK(emissiveCount >= 60);
}

TEST_CASE("CyberCity.Determinism")
{
    using namespace BigHero::Sample::Showcase;

    const CyberCityBuild a = BuildCyberCity(20260921);
    const CyberCityBuild c = BuildCyberCity(20260921);
    REQUIRE(a.objects.size() == c.objects.size());
    bool same = true;
    for (std::size_t i = 0; i < a.objects.size(); ++i)
    {
        const Scene::SceneObject& x = a.objects[i];
        const Scene::SceneObject& y = c.objects[i];
        if (x.position != y.position || x.scale != y.scale || x.tint != y.tint || x.emissive != y.emissive ||
            x.meshId != y.meshId || x.spinSpeed != y.spinSpeed)
        {
            same = false;
            break;
        }
    }
    CHECK(same);

    // 不同种子应产出不同布局（证明种子真的参与生成）
    const CyberCityBuild d = BuildCyberCity(7);
    // 数量由布局常量主导（窗格裁剪受随机楼高影响，故只要求同量级）
    CHECK(d.objects.size() >= a.objects.size() * 8 / 10);
    CHECK(d.objects.size() <= a.objects.size() * 12 / 10);
    bool differs = false;
    for (std::size_t i = 0; i < d.objects.size() && i < a.objects.size(); ++i)
    {
        if (a.objects[i].position != d.objects[i].position || a.objects[i].scale != d.objects[i].scale)
        {
            differs = true;
            break;
        }
    }
    CHECK(differs);
}

TEST_CASE("CyberCity.ExhibitsAndNeons")
{
    using namespace BigHero::Sample::Showcase;

    const CyberCityBuild b = BuildCyberCity();

    // 展台：8 座，均匀分布在半径 13 m 环上，热键 1..8 唯一，主题色非黑
    bool hotkeysUnique = true;
    for (std::size_t i = 0; i < b.exhibits.size(); ++i)
    {
        const CyberExhibit& e = b.exhibits[i];
        const float r = std::sqrt(e.position.x * e.position.x + e.position.z * e.position.z);
        CHECK_NEAR(r, 13.0f, 0.01f);
        CHECK_NEAR(e.position.y, 0.0f, 1e-4f);
        CHECK(e.accent.r + e.accent.g + e.accent.b > 0.3f);
        CHECK(e.hotkey[0] >= '1' && e.hotkey[0] <= '8');
        CHECK(e.name != nullptr && e.name[0] != '\0');
        for (std::size_t j = i + 1; j < b.exhibits.size(); ++j)
        {
            if (std::strcmp(b.exhibits[j].hotkey, e.hotkey) == 0)
                hotkeysUnique = false;
        }
    }
    CHECK(hotkeysUnique);

    // 霓虹：8 盏，位于半径 20 m 环上，强度/半径为正
    for (const NeonLight& n : b.neons)
    {
        const float r = std::sqrt(n.position.x * n.position.x + n.position.z * n.position.z);
        CHECK_NEAR(r, 20.0f, 0.01f);
        CHECK(n.position.y > 2.0f);
        CHECK(n.intensity > 0.0f);
        CHECK(n.radius > 0.0f);
    }
}

TEST_CASE("CyberCity.TimePresets")
{
    using namespace BigHero::Sample::Showcase;

    const std::vector<CyberTimePreset>& ps = CyberCityTimePresets();
    CHECK(ps.size() == 4);
    for (const CyberTimePreset& p : ps)
    {
        CHECK(p.name != nullptr && p.name[0] != '\0');
        CHECK(p.exposure > 0.0f && p.exposure < 5.0f);
        CHECK(p.sunIntensity > 0.0f);
        CHECK(p.ambient > 0.0f);
        CHECK(p.skyIntensity > 0.0f);
        CHECK(p.fogDensity > 0.0f);
        CHECK(glm::length(p.sunDir) > 0.1f);
        CHECK(p.sunDir.y < 0.0f); // 光从上往下照
    }
    // 夜晚：天空最暗、霓虹最亮
    const CyberTimePreset& night = ps[1];
    const CyberTimePreset& noon = ps[3];
    CHECK(night.skyIntensity < noon.skyIntensity);
    CHECK(night.neonBoost > noon.neonBoost);
}

TEST_CASE("CyberCity.SpawnIsWalkable")
{
    using namespace BigHero::Sample::Showcase;
    using namespace BigHero::Game;

    const CyberCityBuild b = BuildCyberCity();

    // 出生点周围不应与任何碰撞盒相交（否则一出生就卡住）
    bool blocked = false;
    for (const BoxCollider& c : b.colliders)
    {
        if (FpController::Overlaps(b.spawn, 0.35f, 1.8f, c))
            blocked = true;
    }
    CHECK(!blocked);

    // 从出生点朝广场中心（-Z）行走 2 秒：应能前进（不被卡住）且落在地面上
    FpController ctrl;
    ctrl.Teleport(b.spawn);
    FpInput input{};
    input.forward = -1.0f; // 朝 -Z（广场中心）
    for (int i = 0; i < 120; ++i)
        ctrl.Update(1.0f / 60.0f, input, b.colliders);
    CHECK(ctrl.OnGround());
    CHECK(ctrl.FeetPosition().z < b.spawn.z - 3.0f); // 至少前进 3 m
    CHECK(ctrl.EyePosition().y > 1.0f);              // 眼高正常（未被顶起/陷落）
}

TEST_CASE("CyberCity.LookPresets")
{
    using namespace BigHero::Sample::Showcase;

    const std::vector<CyberLookPreset>& ls = CyberCityLookPresets();
    CHECK(ls.size() == 4);

    for (const CyberLookPreset& l : ls)
    {
        CHECK(std::strlen(l.name) > 0);
        CHECK(l.bloomStrength >= 0.0f && l.bloomStrength <= 2.0f);
        CHECK(l.bloomThreshold >= 0.0f && l.bloomThreshold <= 2.0f);
        CHECK(l.gradeSaturation >= 0.0f && l.gradeSaturation <= 2.0f);
        CHECK(l.gradeContrast >= 0.5f && l.gradeContrast <= 2.0f);
        CHECK(l.vignetteIntensity >= 0.0f && l.vignetteIntensity <= 1.0f);
        CHECK(l.filmGrain >= 0.0f && l.filmGrain <= 0.5f);
        CHECK(l.fogDensityScale > 0.0f && l.fogDensityScale <= 3.0f);
    }

    // 黑白侦探必须真的去饱和，否则"风格"毫无意义
    CHECK(ls[3].gradeSaturation < 0.2f);
    // 赛博霓虹泛光最强
    CHECK(ls[0].bloomStrength > ls[2].bloomStrength);
}

TEST_CASE("CyberCity.BlendTimePresets")
{
    using namespace BigHero::Sample::Showcase;

    const std::vector<CyberTimePreset>& ps = CyberCityTimePresets();
    CHECK(ps.size() >= 2);
    const CyberTimePreset& dusk = ps[0];
    const CyberTimePreset& night = ps[1];

    // 端点：t=0 应完全等于 a（数值字段），t=1 应完全等于 b
    const CyberTimePreset at0 = BlendTimePresets(dusk, night, 0.0f);
    const CyberTimePreset at1 = BlendTimePresets(dusk, night, 1.0f);
    CHECK(std::fabs(at0.sunIntensity - dusk.sunIntensity) < 1e-5f);
    CHECK(std::fabs(at0.exposure - dusk.exposure) < 1e-5f);
    CHECK(std::fabs(at0.fogDensity - dusk.fogDensity) < 1e-5f);
    CHECK(std::fabs(at1.sunIntensity - night.sunIntensity) < 1e-5f);
    CHECK(std::fabs(at1.exposure - night.exposure) < 1e-5f);
    CHECK(std::fabs(at1.fogDensity - night.fogDensity) < 1e-5f);
    CHECK(std::strcmp(at0.name, dusk.name) == 0);
    CHECK(std::strcmp(at1.name, night.name) == 0);

    // 中段应严格介于两者之间（单调插值）
    const CyberTimePreset mid = BlendTimePresets(dusk, night, 0.5f);
    const float lo = std::min(dusk.sunIntensity, night.sunIntensity);
    const float hi = std::max(dusk.sunIntensity, night.sunIntensity);
    CHECK(mid.sunIntensity >= lo - 1e-5f && mid.sunIntensity <= hi + 1e-5f);
    CHECK(mid.sunIntensity != dusk.sunIntensity);

    // 方向光方向必须保持单位化（插值后重新归一化，否则光照强度会被缩放）
    CHECK(std::fabs(glm::length(mid.sunDir) - 1.0f) < 1e-4f);

    // 越界输入被夹紧，不产生 NaN
    const CyberTimePreset neg = BlendTimePresets(dusk, night, -3.0f);
    const CyberTimePreset over = BlendTimePresets(dusk, night, 9.0f);
    CHECK(std::fabs(neg.sunIntensity - dusk.sunIntensity) < 1e-5f);
    CHECK(std::fabs(over.sunIntensity - night.sunIntensity) < 1e-5f);
    CHECK(!std::isnan(neg.fogDensity) && !std::isnan(over.fogDensity));
}

TEST_CASE("CyberCity.NeonPulse")
{
    using namespace BigHero::Sample::Showcase;

    // 确定性：同 index/time 同结果
    CHECK(NeonPulse(3, 12.5f) == NeonPulse(3, 12.5f));
    CHECK(NeonPulse(0, 1.0f) == NeonPulse(0, 1.0f));

    // 区间：八盏灯在长时间采样内都落在合理范围（不过暗、不过曝）
    float minV = 1e9f;
    float maxV = -1e9f;
    for (uint32_t i = 0; i < 8; ++i)
    {
        for (int s = 0; s < 400; ++s)
        {
            const float v = NeonPulse(i, static_cast<float>(s) * 0.05f);
            CHECK(v >= 0.30f && v <= 1.40f);
            CHECK(!std::isnan(v));
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
    }
    CHECK(minV < 0.95f); // 确实会暗下去（有呼吸/频闪）
    CHECK(maxV > 1.05f); // 确实会亮起来

    // 不同灯柱相位不同（不是整齐划一地同步闪）
    bool anyDiff = false;
    for (int s = 0; s < 60 && !anyDiff; ++s)
    {
        const float t = static_cast<float>(s) * 0.25f;
        if (std::fabs(NeonPulse(0, t) - NeonPulse(1, t)) > 0.05f)
            anyDiff = true;
    }
    CHECK(anyDiff);
}
