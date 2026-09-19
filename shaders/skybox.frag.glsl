#version 450

#include "include/bindings.glsl"
// 天空盒片段：按世界方向采样环境立方图，输出线性 HDR。
// 色调映射/曝光由链路末端统一执行（前向 pp_composite / 延迟 deferred_composite），
// 片元端不再做 ACES——双重色调映射会把暗部压至 ~1/4（历史遗留缺陷 0.17.9 修复）。

struct PointLight
{
    vec3 position;
    float intensity;
    vec3 color;
    float radius;
    float castsShadow; // 1.0 表示启用立方体阴影
    float pad[3];      // std140 数组步长须为 16 的倍数：结构体凑到 48 字节
};

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_LIGHT_UBO, std140) uniform LightUBO {
    vec3 lightDir;
    float dirIntensity;
    vec3 lightColor;
    float ambientFactor;
    vec3 cameraPos;
    float pointLightCount;
    float shadowStrength;
    float shadowBias;
    float iblStrength;
    float exposure;
    mat4 lightSpaceMatrices[4]; // 级联阴影：每级联一个正交光视矩阵
    vec4 cascadeSplits;         // 轴向视图深度分割边界
    vec4 cameraForward;         // xyz=相机前向，w=阴影最远绘制距离
    PointLight lights[8];
} lightUbo;

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_ENV_MAP) uniform samplerCube envMap;

layout(location = 0) in vec3 inPoint;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 dir = normalize(inPoint - lightUbo.cameraPos);
    const vec3 color = texture(envMap, dir).rgb;
    outColor = vec4(color, 1.0);
}
