#pragma once
// 赛博城市展示厅场景（CyberCity）——第一人称沉浸式体验引擎全部特性的程序化场景。
//
// 设计目标：纯 CPU、确定性（同种子同结果）、零 Vulkan / 零窗口依赖，可离线单测，
// 与 samples/open_world 同构（构建期只产出数据，运行期由 Application 装配）。
//
// 场景由「中央广场 + 环形建筑群 + 霓虹灯柱 + 八座特性展台 + 物理游乐区 + 远景天际线」
// 组成；同时导出：
//   - 碰撞盒（供 Game::FpController 做走跳蹲与贴墙滑动）
//   - 霓虹灯参数（8 盏点光源，与灯柱几何一一对应）
//   - 展台清单（位置 / 名称 / 提示 / 数字键热键 / 主题色，供 HUD 与交互系统消费）
//   - 时段预设（黄昏 / 夜晚 / 矩阵蓝 / 正午：方向光 + 环境光 + 曝光 + 天空调色 + 雾）

#include "game/FpController.h"
#include "scene/Scene.h"

#include <cstdint>
#include <string>
#include <vector>

namespace BigHero::Sample::Showcase
{
// 霓虹点光源（与灯柱几何一一对应，上限 = 引擎点光源上限 8）
struct NeonLight
{
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 40.0f;
    float radius = 16.0f;
    bool castsShadow = false; // 默认关（8 盏立方体阴影对移动端过重）
};

// 特性展台：走近或准星指向时 HUD 弹出说明，按 E（或数字键）切换该特性
struct CyberExhibit
{
    glm::vec3 position{0.0f}; // 底座中心（世界坐标，y=0 为地面）
    const char* name = "";    // 特性名
    const char* hint = "";    // 一句话说明
    const char* hotkey = "";  // 快捷键（"1".."9"）
    glm::vec3 accent{1.0f};   // 主题色（自发光展示物 / HUD 高亮）
    int featureId = 0;        // FeatureId 枚举：Application 侧据此切换
};

// 展台对应的特性（与 Application 的开关一一映射）
enum class FeatureId
{
    Bloom = 1,
    VolumetricFog = 2,
    Taa = 3,
    DepthOfField = 4,
    MotionBlur = 5,
    AutoExposure = 6,
    Particles = 7,
    TimeOfDay = 8,
    Physics = 9,
};

// 时段 / 氛围预设（按 T 键循环切换）
struct CyberTimePreset
{
    const char* name = "";
    glm::vec3 sunDir{0.5f, -1.0f, -0.35f}; // 方向光照射方向
    glm::vec3 sunColor{1.0f};
    float sunIntensity = 3.0f;
    float ambient = 0.15f;
    float exposure = 1.0f;
    glm::vec3 skyTint{1.0f}; // 天空盒颜色乘数
    float skyIntensity = 1.0f;
    glm::vec3 fogTint{0.6f};
    float fogDensity = 0.02f;
    float neonBoost = 1.0f; // 霓虹点光源强度倍率
};

// 画面风格 / 调色预设（按 G 键循环切换；复用引擎既有的色调分级 + 泛光 + 暗角 + 颗粒旋钮）
struct CyberLookPreset
{
    const char* name = "";
    float bloomStrength = 0.60f;    // 泛光强度
    float bloomThreshold = 0.80f;   // 泛光阈值（高于该亮度的像素参与扩散）
    float gradeSaturation = 1.00f;  // 色调分级：饱和度
    float gradeContrast = 1.00f;    // 色调分级：对比度
    float gradeLift = 0.00f;        // 色调分级：暗部提升
    float gradeGain = 1.00f;        // 色调分级：亮部增益
    float gradeGamma = 1.00f;       // 色调分级：伽马
    float vignetteIntensity = 0.0f; // 暗角强度 [0,1]
    float filmGrain = 0.0f;         // 胶片颗粒强度
    float fogDensityScale = 1.0f;   // 体积雾密度倍率（在时段雾密度之上再缩放）
};

struct CyberCityBuild
{
    std::vector<Scene::SceneObject> objects;
    std::vector<Game::BoxCollider> colliders;
    std::vector<CyberExhibit> exhibits;
    std::vector<NeonLight> neons;
    glm::vec3 spawn{0.0f, 0.0f, 18.0f}; // 出生点（脚底）
    float spawnYaw = 3.14159265f;       // 出生朝向（弧度，面向广场中心 -Z）
    uint32_t buildingCount = 0;
    uint32_t skylineCount = 0;
};

// 构建赛博城市（确定性：同 seed 同结果）
[[nodiscard]] CyberCityBuild BuildCyberCity(uint32_t seed = 20260921);

// 四套时段预设（循环顺序：黄昏 → 夜晚 → 矩阵蓝 → 正午）
[[nodiscard]] const std::vector<CyberTimePreset>& CyberCityTimePresets();

// 四套画面风格（循环顺序：赛博霓虹 → 电影感 → 冷冽清晨 → 黑白侦探）
[[nodiscard]] const std::vector<CyberLookPreset>& CyberCityLookPresets();

// 时段预设插值（t ∈ [0,1]，smoothstep 缓动）：连续昼夜过渡用，纯逻辑可单测。
// name 取较近的一侧（t ≥ 0.5 取 b），其余数值字段线性插值。
[[nodiscard]] CyberTimePreset BlendTimePresets(const CyberTimePreset& a, const CyberTimePreset& b, float t) noexcept;

// 霓虹呼吸系数（确定性：同 index/time 同结果）。返回 [0.42, 1.30] 区间的强度倍率：
// 慢速呼吸 + 老化灯管的偶发瞬暗（频闪），让八盏灯柱的照明随时间"活着"。
[[nodiscard]] float NeonPulse(uint32_t index, float time) noexcept;

// 出生点安全半径（出生点周围不放置几何，防止卡住）
[[nodiscard]] constexpr float CyberSpawnRadius() noexcept
{
    return 2.5f;
}
} // namespace BigHero::Sample::Showcase
