#version 450
#extension GL_ARB_separate_shader_objects : enable

// 静态光照贴图批次片段着色器（U2-L1 渲染接线 v1）：
// 光照贴图 = 出射辐射度（albedo/直接光/天光 AO 已在烘焙时吸收），不与实时直接光/环境光
// 叠加（与 Unity lightmap 静态几何语义一致）。输出变换与主管线逐位对齐：
// PP 关 = 片元内 exposure+ACES 直通交换链；PP 开 = 线性 HDR 交合成端统一处理。

#include "include/bindings.glsl"

layout(location = 0) in vec2 inLmUV;
layout(location = 0) out vec4 outColor;

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_LIGHTMAP_ATLAS) uniform sampler2D lmAtlas;

// 与 frag.glsl 逐位一致的 PointLight/LightUBO 声明（此处仅消费 exposure）
struct PointLight
{
    vec3 position;
    float intensity;
    vec3 color;
    float radius;
    float castsShadow; // 1.0 表示启用立方体阴影
    float pad[3];      // std140 数组步长须为 16 的倍数：结构体凑到 48 字节
};

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_LIGHT_UBO, std140) uniform LightUBO
{
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
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
    vec4 cameraForward;
    vec4 skyTint;
    PointLight lights[8];
    vec3 probeAmbient;
    float probePadding;
} lightUbo;

// 与 frag.glsl 逐位一致的推送布局（此处仅消费 outputTarget）
layout(push_constant) uniform ObjectPush
{
    int texIndex;
    int normalIndex;
    int mrIndex;
    int emissiveIndex;
    vec3 emissiveFactor;
    float alphaCutoff;
    int mode;
    int outputTarget;
} objPush;

// ACES 近似色调映射（Narkowicz），与 frag.glsl 同式
vec3 acesFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    const vec3 lm = texture(lmAtlas, inLmUV).rgb;
    const vec3 finalColor =
        (objPush.outputTarget == 0) ? acesFilm(lm * lightUbo.exposure) : lm;
    outColor = vec4(finalColor, 1.0);
}
