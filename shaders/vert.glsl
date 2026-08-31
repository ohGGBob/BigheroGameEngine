#version 450
#extension GL_ARB_separate_shader_objects : enable

// 顶点输入（binding0，逐顶点）：位置/法线/UV/顶点色/切线
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec3 inTangent;

// 逐实例输入（binding1，VK_VERTEX_INPUT_RATE_INSTANCE）：
// 每个实例携带完整模型矩阵 + 材质参数（PBR），一次 DrawIndexedInstanced 驱动多实例。
layout(location = 5) in mat4 inModel;
layout(location = 9) in vec4 inTint;
layout(location = 10) in vec4 inMatParams; // x=metallic y=roughness z,w 未用

// set0 binding0：相机视图/投影（std140）
layout(set = 0, binding = 0, std140) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} uboCamera;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outVertColor;
layout(location = 4) out vec3 outTangent;

// 材质参数传递给片段阶段（原经推送常量，现改为逐实例输入后经 varying 传递）
layout(location = 5) out float outMetallic;
layout(location = 6) out float outRoughness;

void main()
{
    const vec4 worldPos = inModel * vec4(inPos, 1.0);
    outWorldPos = worldPos.xyz;

    // 提取模型矩阵左上3x3旋转缩放子矩阵：均匀缩放直接变换法线，非均匀缩放才求逆转置
    const mat3 modelRotScale = mat3(inModel);
    const vec3 scale = vec3(
        length(modelRotScale[0]),
        length(modelRotScale[1]),
        length(modelRotScale[2])
    );
    const float scaleEps = 1e-4;
    const bool uniformScale = all(lessThan(abs(scale - vec3(scale.x)), vec3(scaleEps)));

    vec3 worldNrm;
    if (uniformScale)
    {
        worldNrm = modelRotScale * inNormal;
    }
    else
    {
        worldNrm = transpose(inverse(modelRotScale)) * inNormal;
    }

    // 无分支NaN防护：长度过小回退为向上法线，除法下限兜底
    const float lenNrm = length(worldNrm);
    const vec3 safeNrm = mix(vec3(0, 1, 0), worldNrm, step(1e-6, lenNrm));
    outNormal = safeNrm / max(lenNrm, 1e-6);

    // 切线同样变换到世界空间，片段阶段做正交化构建TBN
    const vec3 worldTangent = modelRotScale * inTangent;
    const float lenTan = length(worldTangent);
    outTangent = mix(vec3(1, 0, 0), worldTangent, step(1e-6, lenTan)) / max(lenTan, 1e-6);

    outUV = inUV;
    outVertColor = inColor * inTint.rgb;

    outMetallic = inMatParams.x;
    outRoughness = inMatParams.y;

    gl_Position = uboCamera.proj * uboCamera.view * worldPos;
}
