#version 450
#extension GL_ARB_separate_shader_objects : enable

// GPU 蒙皮顶点着色器：逐顶点按关节索引采样骨骼调色板合成蒙皮矩阵，
// 在 GPU 完成变形，替代 CPU 蒙皮（O(顶点数 × 关节数) 开销从 CPU 转移到 GPU）。
//
// 调色板由 CPU 每帧求值后整体上传（SkinningPalette -> SkinningUBO），
// 顶点侧只做 4 次矩阵加权求和，因此可支撑大规模角色/大量顶点。

// 顶点输入（binding0，逐顶点）：位置/法线/UV/顶点色/切线 + 权重/关节
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec3 inTangent;
layout(location = 11) in vec4 inWeights;  // 蒙皮权重（和为 1）
layout(location = 12) in uvec4 inJoints;  // 骨骼调色板下标

// 逐实例输入（binding1，VK_VERTEX_INPUT_RATE_INSTANCE）：模型矩阵 + 材质参数
layout(location = 5) in mat4 inModel;
layout(location = 9) in vec4 inTint;
layout(location = 10) in vec4 inMatParams; // x=metallic y=roughness z,w 未用

// set0 binding0：相机视图/投影（std140）
layout(set = 0, binding = 0, std140) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} uboCamera;

// set3 binding0：骨骼矩阵调色板（std140，mat4[128]，数组步长 64 字节）
layout(set = 3, binding = 0, std140) uniform SkinningUBO {
    mat4 boneMatrices[128];
} uboSkin;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outVertColor;
layout(location = 4) out vec3 outTangent;

// 材质参数传递给片段阶段（经 varying 传递，与 vert.glsl 保持一致）
layout(location = 5) out float outMetallic;
layout(location = 6) out float outRoughness;

void main()
{
    // 蒙皮矩阵 = Σ w_i * boneMatrices[joint_i]（4 关节线性混合蒙皮 LBS）
    mat4 skinMat = inWeights.x * uboSkin.boneMatrices[inJoints.x]
                 + inWeights.y * uboSkin.boneMatrices[inJoints.y]
                 + inWeights.z * uboSkin.boneMatrices[inJoints.z]
                 + inWeights.w * uboSkin.boneMatrices[inJoints.w];
    // 权重归一化：防止模型和不足 1 导致整体缩放（除零防护）
    skinMat = skinMat / max(inWeights.x + inWeights.y + inWeights.z + inWeights.w, 1e-6);

    // 绑定空间 -> 蒙皮空间（骨骼变形）
    const vec4 skinnedPos = skinMat * vec4(inPos, 1.0);
    const vec3 skinnedNrm = mat3(skinMat) * inNormal;
    const vec3 skinnedTan = mat3(skinMat) * inTangent;

    // 蒙皮空间 -> 世界空间（逐实例模型矩阵）
    const vec4 worldPos = inModel * skinnedPos;
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
    const mat3 nrmMat = uniformScale ? modelRotScale : transpose(inverse(modelRotScale));

    const vec3 worldNrm = nrmMat * skinnedNrm;
    // 无分支NaN防护：长度过小回退为向上法线，除法下限兜底
    const float lenNrm = length(worldNrm);
    const vec3 safeNrm = mix(vec3(0, 1, 0), worldNrm, step(1e-6, lenNrm));
    outNormal = safeNrm / max(lenNrm, 1e-6);

    const vec3 worldTangent = nrmMat * skinnedTan;
    const float lenTan = length(worldTangent);
    outTangent = mix(vec3(1, 0, 0), worldTangent, step(1e-6, lenTan)) / max(lenTan, 1e-6);

    outUV = inUV;
    outVertColor = inColor * inTint.rgb;

    outMetallic = inMatParams.x;
    outRoughness = inMatParams.y;

    gl_Position = uboCamera.proj * uboCamera.view * worldPos;
}
