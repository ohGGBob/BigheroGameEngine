// 赛博城市展示厅场景构建实现（纯 CPU / 确定性）。
// 布局（俯视，y 向上，单位米）：
//   中央广场半径 ~26 m，八座展台在半径 13 m 环上（每 45°），八根霓虹灯柱在半径 20 m 环上，
//   十栋楼在半径 38~46 m 环上（每 36°），远景天际线 24 栋在半径 60~110 m 随机分布，
//   物理游乐区在 (24, 0, 8)，玩家出生在 (0, 0, 18) 面向广场中心（-Z）。
#include "showcase/CyberCity.h"

#include <cmath>
#include <random>

namespace BigHero::Sample::Showcase
{
namespace
{
// ---- 布局常量 ----
constexpr float kTau = 6.28318530718f;
constexpr float kExhibitRing = 13.0f; // 展台环半径
constexpr float kNeonRing = 20.0f;    // 霓虹灯柱环半径
constexpr float kBuildRing = 38.0f;   // 建筑环半径基准
constexpr float kPlazaRadius = 26.0f; // 广场半径（灯带铺到这里）

// 展台主题（名称 / 说明 / 热键 / 主题色 / 特性）
struct ExhibitDef
{
    const char* name;
    const char* hint;
    const char* hotkey;
    glm::vec3 accent;
    FeatureId id;
};

const ExhibitDef kExhibitDefs[] = {
    {"泛光 Bloom", "霓虹与灯牌溢出光晕——亮部提取 + 高斯模糊 + ACES 合成", "1", {1.00f, 0.82f, 0.45f}, FeatureId::Bloom},
    {"体积雾 · 体积光",
     "16~64 步光线步进高度雾 + CSM 遮挡丁达尔光柱",
     "2",
     {0.45f, 0.78f, 0.95f},
     FeatureId::VolumetricFog},
    {"TAA 时间抗锯齿", "Halton 亚像素抖动 + 历史重投影 + YCoCg AABB 钳制", "3", {0.40f, 1.00f, 0.62f}, FeatureId::Taa},
    {"景深 DoF", "线性深度还原 + 黄金角圆盘采集，按对焦距离虚化", "4", {0.72f, 0.52f, 1.00f}, FeatureId::DepthOfField},
    {"运动模糊",
     "prevVP × inverse(currVP) 重投影求速度，沿轨迹累积采样",
     "5",
     {1.00f, 0.55f, 0.20f},
     FeatureId::MotionBlur},
    {"自动曝光 · 电影化", "对数亮度直方图收敛 + 暗角 + 胶片颗粒", "6", {0.35f, 0.62f, 1.00f}, FeatureId::AutoExposure},
    {"粒子喷泉", "CPU 模拟 + GPU 实例化公告板，按 P 也可触发爆发", "7", {1.00f, 0.42f, 0.72f}, FeatureId::Particles},
    {"昼夜氛围", "方向光 / 环境光 / 曝光 / 天空调色 / 雾一体化换装", "8", {1.00f, 0.80f, 0.30f}, FeatureId::TimeOfDay},
};

// 霓虹配色（青 / 品红 / 琥珀 / 紫，循环复用）
const glm::vec3 kNeonPalette[] = {
    {0.10f, 0.90f, 1.00f},
    {1.00f, 0.16f, 0.62f},
    {1.00f, 0.62f, 0.18f},
    {0.62f, 0.35f, 1.00f},
};

// 小工具：均匀缩放立方体的碰撞盒（立方体边长 = scale，中心在 position）
[[nodiscard]] Game::BoxCollider BoxOf(const glm::vec3& center, float scale)
{
    return Game::BoxCollider{center, glm::vec3(scale * 0.5f)};
}
} // namespace

const std::vector<CyberTimePreset>& CyberCityTimePresets()
{
    static const std::vector<CyberTimePreset> presets = {
        // 黄昏：低角度暖阳 + 橙紫雾，霓虹半亮
        {"黄昏 Dusk",
         {0.85f, -0.35f, -0.45f},
         {1.00f, 0.68f, 0.42f},
         3.4f,
         0.30f,
         1.25f,
         {1.00f, 0.78f, 0.62f},
         0.95f,
         {0.42f, 0.30f, 0.34f},
         0.020f,
         1.0f},
        // 夜晚：弱月光 + 深蓝天 + 浓雾，霓虹全亮（赛博朋克主视觉）
        // 注意：ambient/exposure 经过视觉校准——旧值 0.10/1.25 导致整体过暗，已提升到可辨识暗部
        {"夜晚 Night",
         {0.35f, -0.85f, -0.40f},
         {0.52f, 0.66f, 1.00f},
         1.20f,
         0.28f,
         1.55f,
         {0.22f, 0.28f, 0.55f},
         0.65f,
         {0.08f, 0.10f, 0.18f},
         0.028f,
         1.6f},
        // 矩阵蓝：冷色顶光 + 青雾，霓虹偏冷
        {"矩阵蓝 Matrix",
         {0.20f, -1.00f, 0.15f},
         {0.50f, 0.85f, 1.00f},
         1.8f,
         0.25f,
         1.40f,
         {0.42f, 0.78f, 1.00f},
         0.75f,
         {0.08f, 0.18f, 0.26f},
         0.022f,
         1.3f},
        // 正午：高角度白光 + 稀薄雾，霓虹压暗（展示白天观感与阴影质量）
        {"正午 Noon",
         {0.30f, -1.00f, 0.20f},
         {1.00f, 0.98f, 0.94f},
         4.2f,
         0.32f,
         0.95f,
         {1.00f, 1.00f, 1.00f},
         1.00f,
         {0.62f, 0.66f, 0.72f},
         0.012f,
         0.7f},
    };
    return presets;
}

const std::vector<CyberLookPreset>& CyberCityLookPresets()
{
    static const std::vector<CyberLookPreset> looks = {
        // 赛博霓虹：高饱和 + 强泛光 + 轻暗角（默认，配合夜晚时段）
        {"赛博霓虹 Cyber", 0.85f, 0.72f, 1.18f, 1.06f, 0.010f, 1.06f, 0.98f, 0.28f, 0.020f, 1.00f},
        // 电影感：柔对比 + 抬暗部 + 宽暗角 + 颗粒（电影调色）
        {"电影感 Cinematic", 0.55f, 0.85f, 0.92f, 1.10f, 0.022f, 1.02f, 1.06f, 0.46f, 0.055f, 1.25f},
        // 冷冽清晨：低饱和冷调 + 稀雾 + 几乎无暗角（看清几何与材质细节）
        {"冷冽清晨 Clean", 0.40f, 0.92f, 0.88f, 1.04f, 0.004f, 1.04f, 1.00f, 0.10f, 0.000f, 0.70f},
        // 黑白侦探：去饱和 + 高对比硬调 + 重暗角（展示色调分级极限）
        {"黑白侦探 Noir", 0.50f, 0.78f, 0.06f, 1.34f, 0.030f, 1.12f, 0.88f, 0.62f, 0.090f, 1.10f},
    };
    return looks;
}

CyberTimePreset BlendTimePresets(const CyberTimePreset& a, const CyberTimePreset& b, float t) noexcept
{
    const float u = std::clamp(t, 0.0f, 1.0f);
    const float s = u * u * (3.0f - 2.0f * u); // smoothstep 缓动（起止无速度突变）
    auto lerp3 = [s](const glm::vec3& x, const glm::vec3& y) { return glm::mix(x, y, s); };
    auto lerp1 = [s](float x, float y) { return x + (y - x) * s; };

    CyberTimePreset out{};
    out.name = (u >= 0.5f) ? b.name : a.name;
    out.sunDir = glm::normalize(lerp3(a.sunDir, b.sunDir));
    out.sunColor = lerp3(a.sunColor, b.sunColor);
    out.sunIntensity = lerp1(a.sunIntensity, b.sunIntensity);
    out.ambient = lerp1(a.ambient, b.ambient);
    out.exposure = lerp1(a.exposure, b.exposure);
    out.skyTint = lerp3(a.skyTint, b.skyTint);
    out.skyIntensity = lerp1(a.skyIntensity, b.skyIntensity);
    out.fogTint = lerp3(a.fogTint, b.fogTint);
    out.fogDensity = lerp1(a.fogDensity, b.fogDensity);
    out.neonBoost = lerp1(a.neonBoost, b.neonBoost);
    return out;
}

float NeonPulse(uint32_t index, float time) noexcept
{
    // 确定性哈希（避免引入 <random> 状态，保证同 index/time 同结果）
    auto hash01 = [](uint32_t x) noexcept -> float
    {
        uint32_t h = x * 2654435761u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        return static_cast<float>((h & 0xFFFFu)) / 65535.0f;
    };

    const float phase = hash01(index * 7u + 1u) * kTau;          // 每盏灯独立相位
    const float rate = 0.55f + hash01(index * 13u + 5u) * 1.10f; // 慢呼吸角频率
    float v = 1.0f + 0.26f * std::sin(time * rate + phase);

    // 老化灯管瞬暗：高频正弦过阈值时短暂压到 55%（不是硬跳变，视觉上是"抖一下"）
    const float flickerRate = 4.5f + hash01(index * 29u + 3u) * 6.5f;
    const float f = std::sin(time * flickerRate + phase * 3.0f);
    if (f > 0.955f)
        v *= 0.55f;
    return std::clamp(v, 0.30f, 1.40f);
}

CyberCityBuild BuildCyberCity(uint32_t seed)
{
    CyberCityBuild out{};
    std::vector<Scene::SceneObject>& objs = out.objects;
    std::vector<Game::BoxCollider>& cols = out.colliders;

    std::mt19937 rng(seed);
    auto randF = [&rng](float lo, float hi) { return lo + static_cast<float>(rng() % 10000) / 10000.0f * (hi - lo); };
    auto randI = [&rng](int lo, int hi) { return lo + static_cast<int>(rng() % static_cast<uint32_t>(hi - lo + 1)); };

    auto push = [&objs](glm::vec3 pos, float scale, glm::vec3 tint, float metallic, float roughness,
                        glm::vec3 emissive = glm::vec3(0.0f), uint32_t meshId = 0, float spin = 0.0f,
                        float phase = 0.0f)
    {
        Scene::SceneObject o;
        o.position = pos;
        o.scale = scale;
        o.tint = tint;
        o.spinSpeed = spin;
        o.phase = phase;
        o.meshId = meshId;
        o.metallic = metallic;
        o.roughness = roughness;
        o.emissive = emissive;
        objs.push_back(o);
    };

    // ================= 1. 地面导光带（放射状霓虹地缝，夜里引导视线） =================
    for (int i = 0; i < 8; ++i)
    {
        const float ang = kTau * static_cast<float>(i) / 8.0f + 0.3926991f; // 与展台错开 22.5°
        const glm::vec2 dir(std::cos(ang), std::sin(ang));
        const glm::vec3 tint = kNeonPalette[i % 4];
        for (int s = 0; s < 7; ++s)
        {
            const float r = 4.0f + static_cast<float>(s) * 3.2f;
            const glm::vec3 p(dir.x * r, 0.05f, dir.y * r);
            push(p, 0.30f, glm::vec3(0.06f), 0.0f, 0.6f, tint * 1.15f);
        }
    }

    // ================= 2. 中央雕塑（金属圆环 + 悬浮核心） =================
    {
        push(glm::vec3(0.0f, 1.5f, 0.0f), 3.0f, glm::vec3(0.10f, 0.11f, 0.14f), 0.85f, 0.35f); // 基座
        cols.push_back(BoxOf(glm::vec3(0.0f, 1.5f, 0.0f), 3.0f));
        // 悬浮金属圆环（自转）
        push(glm::vec3(0.0f, 4.6f, 0.0f), 2.4f, glm::vec3(0.95f, 0.78f, 0.36f), 1.0f, 0.14f,
             glm::vec3(0.10f, 0.07f, 0.02f), 1, 28.0f, 0.0f);
        // 悬浮核心（青色自发光球）
        push(glm::vec3(0.0f, 6.6f, 0.0f), 0.55f, glm::vec3(1.0f), 0.0f, 1.0f, glm::vec3(0.35f, 1.5f, 1.8f), 3, 0.0f,
             0.0f);
    }

    // ================= 3. 八座特性展台（底座 + 立柱 + 悬浮展示物 + 光环 + 标牌） =================
    for (int i = 0; i < 8; ++i)
    {
        const ExhibitDef& def = kExhibitDefs[i];
        const float ang = kTau * static_cast<float>(i) / 8.0f + 0.3926991f;
        const glm::vec3 base(std::cos(ang) * kExhibitRing, 0.0f, std::sin(ang) * kExhibitRing);
        const glm::vec2 outward(std::cos(ang), std::sin(ang)); // 由广场中心指向展台

        // 底座 + 立柱
        push(glm::vec3(base.x, 0.9f, base.z), 1.8f, glm::vec3(0.09f, 0.10f, 0.13f), 0.80f, 0.38f);
        cols.push_back(BoxOf(glm::vec3(base.x, 0.9f, base.z), 1.8f));
        push(glm::vec3(base.x, 2.35f, base.z), 0.55f, glm::vec3(0.12f, 0.13f, 0.16f), 0.90f, 0.30f);

        // 悬浮展示物（三种网格轮换：球 / 胶囊 / 圆环）
        const uint32_t meshId = (i % 3 == 0) ? 3u : ((i % 3 == 1) ? 4u : 1u);
        push(glm::vec3(base.x, 3.25f, base.z), 0.95f, def.accent, 0.35f, 0.22f, def.accent * 1.35f, meshId, 34.0f,
             static_cast<float>(i) * 37.0f);
        // 顶部光环（强自发光，远处也能看见）
        push(glm::vec3(base.x, 4.15f, base.z), 0.30f, glm::vec3(1.0f), 0.0f, 1.0f, def.accent * 2.6f, 3);
        // 朝外的标牌（薄板，自发光描边观感）
        push(glm::vec3(base.x + outward.x * 1.15f, 2.35f, base.z + outward.y * 1.15f), 0.85f, def.accent, 0.2f, 0.5f,
             def.accent * 0.85f);

        CyberExhibit ex;
        ex.position = base;
        ex.name = def.name;
        ex.hint = def.hint;
        ex.hotkey = def.hotkey;
        ex.accent = def.accent;
        ex.featureId = static_cast<int>(def.id);
        out.exhibits.push_back(ex);
    }

    // ================= 4. 八根霓虹灯柱（柱体 + 顶部灯球 + 点光源） =================
    for (int i = 0; i < 8; ++i)
    {
        const float ang = kTau * static_cast<float>(i) / 8.0f;
        const glm::vec3 p(std::cos(ang) * kNeonRing, 0.0f, std::sin(ang) * kNeonRing);
        const glm::vec3 tint = kNeonPalette[i % 4];

        // 柱体：4 段小立方体堆叠（均匀缩放网格，故用堆叠实现细高柱）
        for (int s = 0; s < 4; ++s)
            push(glm::vec3(p.x, 0.35f + static_cast<float>(s) * 0.70f, p.z), 0.65f, glm::vec3(0.07f, 0.08f, 0.10f),
                 0.85f, 0.35f);
        cols.push_back(Game::BoxCollider{glm::vec3(p.x, 1.4f, p.z), glm::vec3(0.4f, 1.4f, 0.4f)});
        // 顶部灯球（强自发光）
        push(glm::vec3(p.x, 3.15f, p.z), 0.42f, glm::vec3(1.0f), 0.0f, 1.0f, tint * 3.2f, 3);

        NeonLight n;
        n.position = glm::vec3(p.x, 3.15f, p.z);
        n.color = tint;
        n.intensity = 42.0f;
        n.radius = 18.0f;
        n.castsShadow = false;
        out.neons.push_back(n);
    }

    // ================= 5. 环形建筑群（十栋，竖直堆叠 + 朝内窗格） =================
    for (int i = 0; i < 10; ++i)
    {
        const float ang = kTau * static_cast<float>(i) / 10.0f + 0.3141593f;
        const float dist = kBuildRing + randF(0.0f, 8.0f);
        const float x = std::cos(ang) * dist;
        const float z = std::sin(ang) * dist;
        const float s = randF(7.0f, 12.0f); // 楼体边长（均匀缩放）
        const int floors = 2 + randI(0, 2); // 2~4 层堆叠
        const float totalH = s * static_cast<float>(floors);
        const glm::vec3 bodyTint(randF(0.07f, 0.13f), randF(0.08f, 0.14f), randF(0.12f, 0.20f));

        for (int f = 0; f < floors; ++f)
        {
            const float y = s * (static_cast<float>(f) + 0.5f);
            push(glm::vec3(x, y, z), s, bodyTint, 0.72f, 0.36f);
        }
        cols.push_back(Game::BoxCollider{glm::vec3(x, totalH * 0.5f, z), glm::vec3(s * 0.5f, totalH * 0.5f, s * 0.5f)});
        ++out.buildingCount;

        // 楼顶航空障碍灯（红色自发光小球，夜里勾出天际线轮廓）
        push(glm::vec3(x, totalH + s * 0.09f, z), s * 0.13f, glm::vec3(1.0f), 0.0f, 1.0f,
             glm::vec3(1.70f, 0.14f, 0.10f), 3);

        // 朝内窗格：楼体朝广场一面的自发光小方块（2 列 × 3 行）
        const glm::vec2 inward(-std::cos(ang), -std::sin(ang));
        const glm::vec2 side(-inward.y, inward.x);
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 2; ++col)
            {
                const float y = s * (0.6f + static_cast<float>(row) * 0.9f);
                if (y > totalH - s * 0.35f)
                    continue;
                const float lateral = (static_cast<float>(col) - 0.5f) * s * 0.45f;
                const glm::vec3 wp(x + inward.x * (s * 0.5f + 0.10f) + side.x * lateral, y,
                                   z + inward.y * (s * 0.5f + 0.10f) + side.y * lateral);
                const glm::vec3 wint = kNeonPalette[(i + row + col) % 4];
                push(wp, s * 0.16f, glm::vec3(1.0f), 0.0f, 1.0f, wint * randF(1.1f, 2.2f));
            }
        }
    }

    // ================= 6. 物理游乐区（静态台 + 动态方块堆） =================
    {
        const glm::vec3 table(24.0f, 0.0f, 8.0f);
        push(glm::vec3(table.x, 1.5f, table.z), 3.0f, glm::vec3(0.14f, 0.15f, 0.18f), 0.6f, 0.45f);
        cols.push_back(BoxOf(glm::vec3(table.x, 1.5f, table.z), 3.0f));

        for (int i = 0; i < 8; ++i)
        {
            Scene::SceneObject o;
            o.position = glm::vec3(table.x + randF(-1.6f, 1.6f), 3.6f + static_cast<float>(i) * 0.95f,
                                   table.z + randF(-1.6f, 1.6f));
            o.scale = 0.75f;
            o.tint = kNeonPalette[i % 4] * 0.6f;
            o.meshId = 0;
            o.metallic = 0.35f;
            o.roughness = 0.35f;
            o.emissive = kNeonPalette[i % 4] * 0.35f;
            o.physicsType = Physics::BodyType::Dynamic;
            o.physicsShape = Physics::ShapeType::Box;
            o.physicsMass = 1.5f;
            o.physicsFriction = 0.55f;
            o.physicsRestitution = 0.25f;
            objs.push_back(o);
        }
    }

    // ================= 7. 远景天际线（纯剪影，无碰撞，营造纵深） =================
    for (int i = 0; i < 24; ++i)
    {
        const float ang = randF(0.0f, kTau);
        const float dist = randF(60.0f, 110.0f);
        const float s = randF(8.0f, 18.0f);
        const int floors = 1 + randI(0, 2);
        const glm::vec3 p(std::cos(ang) * dist, 0.0f, std::sin(ang) * dist);
        const glm::vec3 tint(randF(0.05f, 0.09f), randF(0.06f, 0.10f), randF(0.09f, 0.15f));
        for (int f = 0; f < floors; ++f)
            push(glm::vec3(p.x, s * (static_cast<float>(f) + 0.5f), p.z), s, tint, 0.5f, 0.55f, glm::vec3(0.0f), 0,
                 0.0f, 0.0f);
        ++out.skylineCount;
    }

    return out;
}
} // namespace BigHero::Sample::Showcase
