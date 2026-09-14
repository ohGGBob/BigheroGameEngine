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
layout(location = 7) in float inVertAlpha;

// 仅需反照率与法线贴图采样（法线贴图在 GBuffer 阶段就烘焙成世界法线）
layout(set = 1, binding = 1) uniform sampler2D albedoTex;
layout(set = 1, binding = 2) uniform sampler2D normalTex;
// 逐物体纹理池（16槽）：索引来自推送常量（动态均匀），与 forward frag.glsl 一致
layout(set = 1, binding = 9) uniform sampler2D uObjectTex[16];

// 逐材质推送常量：纹理池槽位 + 透明参数（与 frag.glsl 布局一致；GBuffer 阶段只关心 OPAQUE/MASK，
// BLEND 由光照后的透明叠加通道处理，自发光由该通道以加性混合补写）
layout(push_constant) uniform ObjectPush
{
    int texIndex;        // 反照率贴图槽
    int normalIndex;     // 法线贴图槽
    int mrIndex;         // metallicRoughness 贴图槽（无贴图时指向纯白：g=b=1 透传因子）
    int emissiveIndex;   // 自发光贴图槽（GBuffer 阶段不采样，保持布局对齐）
    vec3 emissiveFactor; // 自发光倍率（GBuffer 阶段不采样，保持布局对齐）
    float alphaCutoff;   // MASK 裁剪阈值（<=0 视为不透明）
    int mode;            // 0=OPAQUE 1=MASK 2=BLEND 3=EMISSIVE_ONLY
} objPush;

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

    vec3 mapped = texture(uObjectTex[objPush.normalIndex], inUV).xyz * 2.0 - 1.0;
    N = normalize(TBN * mapped);

    // ---- 材质参数 ----
    // 反照率 = 顶点色(tint) × 反照率贴图；金属度/粗糙度 = 因子 × metallicRoughness 贴图通道
    // （glTF 2.0 约定：mr 贴图 G=粗糙度 B=金属度；无贴图时指向纯白槽，因子原样透传）
    const vec4 baseTex = texture(uObjectTex[objPush.texIndex], inUV);
    const vec3 albedo = inVertColor * baseTex.rgb;
    const float alpha = inVertAlpha * baseTex.a;
    const vec4 mrSample = texture(uObjectTex[objPush.mrIndex], inUV);
    const float metallic = clamp(inMetallic * mrSample.b, 0.0, 1.0);
    const float roughness = clamp(inRoughness * mrSample.g, 0.045, 1.0);

    // glTF 透明：MASK 按阈值裁剪（BLEND 不进 GBuffer，由透明叠加通道处理）
    if (objPush.mode == 1 && alpha < objPush.alphaCutoff)
        discard;

    outAlbedo = vec4(albedo, metallic);
    outNormal = vec4(N, roughness);
    outPosition = vec4(inWorldPos, 1.0); // a=1 标记几何像素
}
