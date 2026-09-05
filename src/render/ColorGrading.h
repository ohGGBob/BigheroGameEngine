#pragma once
// 色调分级（Color Grading）纯逻辑核心——无 GPU / Vulkan 依赖，可离线单测。
//
// 设计：
// - ColorGradeParams：ASC CDL 风格分级参数（gain/lift + gamma + 对比度 + 饱和度），
//   默认值均为"无操作"，编辑器不改时画面不变。
// - GradeColor()：对单像素颜色应用分级，顺序固定为
//     gain*color + lift  →  伽马幂次  →  围绕中灰的对比度  →  按亮度饱和度混合
//   该运算符与 GPU 合成着色器 pp_composite.frag.glsl 中的 gradeColor 保持同一公式，
//   单测据此校验公式正确性（GPU 侧仅 compile-verify，无法运行）。
//
// 该模块由升级 21 新增，把"后处理扩展·色调分级"的可单测数学从渲染管线中剥离。

#include <glm/glm.hpp>

namespace BigHero::Render
{
// 色调分级参数（作用于显示参考颜色，典型在 ACES 色调映射之后）
struct ColorGradeParams
{
    float saturation = 1.0f; // 饱和度：1=不变，0=全灰，>1 更艳
    float contrast = 1.0f;   // 对比度：以中灰(0.5)为中心，1=不变
    float lift = 0.0f;       // 加性偏移（暗部提升），三通道统一
    float gain = 1.0f;       // 乘性缩放（整体亮度），1=不变
    float gamma = 1.0f;      // 幂次（<1 提亮中间调，>1 压暗），1=不变
};

// 对单像素颜色应用分级。返回分级后颜色（各通道 >= 0）。
// 该实现与 GPU 合成着色器 pp_composite.frag.glsl 的 gradeColor 完全一致。
[[nodiscard]] inline glm::vec3 GradeColor(const glm::vec3& c, const ColorGradeParams& p) noexcept
{
    // 1) gain * color + lift
    glm::vec3 x = c * p.gain + glm::vec3(p.lift);
    // 2) 伽马幂次（负数夹 0 防 pow 产生 NaN）
    x = glm::max(x, glm::vec3(0.0f));
    x = glm::pow(x, glm::vec3(p.gamma));
    // 3) 对比度：围绕中灰(0.5)
    x = (x - glm::vec3(0.5f)) * p.contrast + glm::vec3(0.5f);
    // 4) 饱和度：按亮度混回（Rec.709 亮度权重）
    const float luma = glm::dot(x, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    x = glm::mix(glm::vec3(luma), x, p.saturation);
    return glm::max(x, glm::vec3(0.0f));
}
} // namespace BigHero::Render
