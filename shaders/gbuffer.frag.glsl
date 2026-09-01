#version 450
#extension GL_ARB_separate_shader_objects : enable

// GBuffer 几何通道片段着色器：把材质/法线/世界坐标写入多渲染目标（MRT），
// 真正的光照在延迟光照通道完成。顶点输入与 forward 的 vert.glsl 完全一致，
// 仅此处输出 3 个颜色附件而非最终颜色。

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inVertColor;
layout(location = 4) in vec3 inTangent;
// 材质参数（逐实例输入经顶点阶段传递）
layout(location = 5) in float inMetallic;
layout(location = 6) in float inRoughness;

// 仅需反照率与法线贴图采样（法线贴图在 GBuffer 阶段就烘焙成世界法线）
layout(set = 1, binding = 1) uniform sampler2D albedoTex;
layout(set = 1, binding = 2) uniform sampler2D normalTex;

// 多渲染目标输出（对应延迟渲染通道的子通道 0）
layout(location = 0) out vec4 outAlbedo;   // rgb = 反照率, a = 金属度
layout(location = 1) out vec4 outNormal;   // rgb = 世界法线, a = 粗糙度
layout(location = 2) out vec4 outPosition; // rgb = 世界坐标, a = 1（几何）/0（背景）

void main()
{
    // ---- TBN 构建与法线贴图（烘焙为世界空间法线写入 GBuffer） ----
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent - N * dot(N, inTangent)); // Gram-Schmidt 正交化
    vec3 B = cross(N, T);
    const mat3 TBN = mat3(T, B, N);

    vec3 mapped = texture(normalTex, inUV).xyz * 2.0 - 1.0;
    N = normalize(TBN * mapped);

    // ---- 材质参数 ----
    const vec3 albedo = inVertColor * texture(albedoTex, inUV).rgb;
    const float metallic = clamp(inMetallic, 0.0, 1.0);
    const float roughness = clamp(inRoughness, 0.045, 1.0);

    outAlbedo = vec4(albedo, metallic);
    outNormal = vec4(N, roughness);
    outPosition = vec4(inWorldPos, 1.0); // a=1 标记几何像素
}
