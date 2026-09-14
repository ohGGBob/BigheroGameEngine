#version 450
// 天空盒片段：按世界方向采样环境立方图，与场景同款ACES色调映射

struct PointLight
{
    vec3 position;
    float intensity;
    vec3 color;
    float radius;
    float castsShadow; // 1.0 表示启用立方体阴影
    float pad[3];      // std140 数组步长须为 16 的倍数：结构体凑到 48 字节
};

layout(set = 1, binding = 0, std140) uniform LightUBO {
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

layout(set = 1, binding = 4) uniform samplerCube envMap;

layout(location = 0) in vec3 inPoint;

layout(location = 0) out vec4 outColor;

vec3 acesFilm(vec3 x)
{
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    const vec3 dir = normalize(inPoint - lightUbo.cameraPos);
    const vec3 color = texture(envMap, dir).rgb;
    outColor = vec4(acesFilm(color), 1.0);
}
