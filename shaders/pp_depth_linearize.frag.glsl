#version 450
// 线性深度还原：采样 MSAA 深度图（sampler2DMS），对全部样本取均值，
// 按 Vulkan NDC z ∈ [0,1] 反算正向视线距离（米），写入 R32F 线性深度图。
// 景深着色器据此计算弥散圆（CoC）。
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2DMS sceneDepth;

layout(push_constant) uniform Params
{
    float nearPlane;
    float farPlane;
    float pad0;
    float pad1;
} params;

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    int samples = textureSamples(sceneDepth);
    float depth = 0.0;
    for (int i = 0; i < samples; ++i)
        depth += texelFetch(sceneDepth, coord, i).r;
    depth /= float(samples);

    // Vulkan NDC z ∈ [0,1]：映射到 [-1,1] 再按透视反算视线距离
    float ndc = 2.0 * depth - 1.0;
    float zView = (2.0 * params.nearPlane * params.farPlane) /
                  (params.farPlane + params.nearPlane - ndc * (params.farPlane - params.nearPlane));

    outColor = vec4(zView, 0.0, 0.0, 1.0);
}
