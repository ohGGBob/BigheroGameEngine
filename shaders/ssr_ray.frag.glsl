#version 450
#extension GL_ARB_separate_shader_objects : enable

// SSR 射线检测片段着色器：
// 从 GBuffer 重建世界坐标与法线，计算反射方向，在屏幕空间 ray march，
// 命中时采样场景颜色，输出反射颜色 + 命中强度（alpha）。

layout(set = 0, binding = 0) uniform sampler2D gPosition;  // rgb=世界坐标, a=几何标记
layout(set = 0, binding = 1) uniform sampler2D gNormal;    // rgb=世界法线, a=粗糙度
layout(set = 0, binding = 2) uniform sampler2D sceneColor; // 离屏场景颜色（光照输出）

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outReflection;

layout(push_constant) uniform PushConstants
{
    mat4 viewProj;     // 0, 64B：视投影矩阵
    vec3 cameraPos;    // 64, 12B：相机世界坐标
    float maxDistance; // 76, 4B：射线最大距离
    float stepSize;    // 80, 4B：初始步长
    float thickness;   // 84, 4B：厚度容差
    float edgeFade;    // 88, 4B：屏幕边缘淡出
    int stepCount;     // 92, 4B：步数
    float pad0;        // 96
    float pad1;        // 100
}
pc;

// 屏幕边缘淡出：靠近屏幕边缘的反射逐渐减弱
float EdgeFade(vec2 uv)
{
    const float fade = pc.edgeFade;
    vec2 factor = smoothstep(vec2(0.0), vec2(fade), uv) * smoothstep(vec2(0.0), vec2(fade), vec2(1.0) - uv);
    return factor.x * factor.y;
}

void main()
{
    const vec4 posData = texture(gPosition, inUV);
    // 背景像素：无反射
    if (posData.a <= 0.0)
    {
        outReflection = vec4(0.0);
        return;
    }

    const vec3 worldPos = posData.rgb;
    const vec4 normalData = texture(gNormal, inUV);
    const vec3 N = normalize(normalData.rgb);
    const float roughness = normalData.a;

    // 视线方向（从表面指向相机）
    const vec3 V = normalize(pc.cameraPos - worldPos);
    // 反射方向
    const vec3 R = reflect(-V, N);

    // 菲涅尔：掠射角反射更强
    const float fresnel = pow(1.0 - max(dot(V, N), 0.0), 3.0);
    // 粗糙度越高反射越弱
    const float roughnessFade = 1.0 - roughness;

    // Ray march：在世界空间沿反射方向步进
    vec3 hitColor = vec3(0.0);
    float hitStrength = 0.0;

    const float stepSize = pc.maxDistance / float(pc.stepCount);
    vec3 rayPos = worldPos + N * 0.05; // 偏移避免自相交

    for (int i = 0; i < pc.stepCount; ++i)
    {
        rayPos += R * stepSize;

        // 投影到屏幕空间
        const vec4 clip = pc.viewProj * vec4(rayPos, 1.0);
        if (clip.w <= 0.0)
            break; // 射线在相机后方
        const vec2 screenUV = (clip.xy / clip.w) * 0.5 + 0.5;

        // 屏幕外：终止
        if (screenUV.x < 0.0 || screenUV.x > 1.0 || screenUV.y < 0.0 || screenUV.y > 1.0)
            break;

        // 采样场景深度（通过位置纹理的几何标记和距离）
        const vec4 scenePosData = texture(gPosition, screenUV);
        if (scenePosData.a <= 0.0)
            continue; // 背景，继续步进

        const vec3 scenePos = scenePosData.rgb;
        const float rayDistance = length(rayPos - pc.cameraPos);
        const float sceneDistance = length(scenePos - pc.cameraPos);

        // 深度测试：射线点在场景几何之后 → 命中
        if (sceneDistance < rayDistance && (rayDistance - sceneDistance) < pc.thickness + stepSize * 2.0)
        {
            // 命中：采样场景颜色
            hitColor = texture(sceneColor, screenUV).rgb;
            // 距离淡出
            const float distFade = 1.0 - clamp(length(rayPos - worldPos) / pc.maxDistance, 0.0, 1.0);
            // 边缘淡出
            const float edge = EdgeFade(screenUV);
            hitStrength = distFade * edge;
            break;
        }
    }

    // 最终反射强度 = 命中强度 × 菲涅尔 × 粗糙度衰减
    const float finalStrength = hitStrength * fresnel * roughnessFade;
    outReflection = vec4(hitColor * finalStrength, finalStrength);
}
