#version 450
#extension GL_ARB_separate_shader_objects : enable

// 顶点输入：位置/法线/UV/顶点色
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inColor;

// set0 binding0：相机视图/投影（std140）
layout(set = 0, binding = 0, std140) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} uboCamera;

// 推送常量：逐物体的模型矩阵 + 顶点色乘数
layout(push_constant) uniform PushModel {
    mat4 model;
    vec4 tint;
} pushObject;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outVertColor;

void main()
{
    const vec4 worldPos = pushObject.model * vec4(inPos, 1.0);
    outWorldPos = worldPos.xyz;

    // 提取模型矩阵左上3x3旋转缩放子矩阵：均匀缩放直接变换法线，非均匀缩放才求逆转置
    const mat3 modelRotScale = mat3(pushObject.model);
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

    outUV = inUV;
    outVertColor = inColor * pushObject.tint.rgb;

    gl_Position = uboCamera.proj * uboCamera.view * worldPos;
}
