#version 450
#extension GL_ARB_separate_shader_objects : enable

// 反射探针捕获顶点着色器（U2-L2 GPU 立方图捕获 v1）：
// 与主 vert.glsl 同构（同一顶点布局/实例属性/ varying 契约），唯一差异是
// 视图投影不走 set0 相机 UBO，而走顶点阶段推送常量（每面一次推送）——
// 捕获 pass 与主 pass 共用同一批描述符集与实例缓冲，避免相机 UBO 的读写冲突。

#include "include/bindings.glsl"

// 顶点输入（binding0，逐顶点）：位置/法线/UV/顶点色/切线
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec3 inTangent;

// 逐实例输入（binding1）：模型矩阵 + 材质参数（与主管线一致）
layout(location = 5) in mat4 inModel;
layout(location = 9) in vec4 inTint;
layout(location = 10) in vec4 inMatParams;
layout(location = 13) in vec4 inEmissive;
layout(location = 14) in vec4 inProbeIrradiance;

// 顶点阶段推送常量：捕获面的视图投影矩阵（64B）
layout(push_constant) uniform CapturePush
{
    mat4 faceVP;
} pcCapture;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outVertColor;
layout(location = 4) out vec3 outTangent;
layout(location = 5) out float outMetallic;
layout(location = 6) out float outRoughness;
layout(location = 7) out float outVertAlpha;
layout(location = 8) out vec3 outEmissive;
layout(location = 9) out vec3 outProbeIrradiance;

void main()
{
    const vec4 worldPos = inModel * vec4(inPos, 1.0);
    outWorldPos = worldPos.xyz;

    const mat3 modelRotScale = mat3(inModel);
    const vec3 scale = vec3(length(modelRotScale[0]), length(modelRotScale[1]), length(modelRotScale[2]));
    const bool uniformScale = all(lessThan(abs(scale - vec3(scale.x)), vec3(1e-4)));

    vec3 worldNrm;
    if (uniformScale)
    {
        worldNrm = modelRotScale * inNormal;
    }
    else
    {
        worldNrm = transpose(inverse(modelRotScale)) * inNormal;
    }
    const float lenNrm = length(worldNrm);
    const vec3 safeNrm = mix(vec3(0, 1, 0), worldNrm, step(1e-6, lenNrm));
    outNormal = safeNrm / max(lenNrm, 1e-6);

    const vec3 worldTangent = modelRotScale * inTangent;
    const float lenTan = length(worldTangent);
    outTangent = mix(vec3(1, 0, 0), worldTangent, step(1e-6, lenTan)) / max(lenTan, 1e-6);

    outUV = inUV;
    outVertColor = inColor * inTint.rgb;
    outMetallic = inMatParams.x;
    outRoughness = inMatParams.y;
    outVertAlpha = inTint.w;
    outEmissive = max(inEmissive.rgb, vec3(0.0));
    outProbeIrradiance = max(inProbeIrradiance.rgb, vec3(0.0));

    gl_Position = pcCapture.faceVP * worldPos;
}
