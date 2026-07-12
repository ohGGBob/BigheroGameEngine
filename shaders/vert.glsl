#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_450pack : enable

// 顶点输入：位置/法线/UV/顶点色
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inColor;

// std140 UBO 相机常量缓冲，GPU/CPU 16字节对齐严格匹配
layout(set = 0, binding = 0, std140) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
} uboCamera;

// 输出片元着色器：仅UV关闭透视校正插值，其余保留透视插值避免光照扭曲
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out nonperspective vec2 outUV;
layout(location = 3) out vec3 outVertColor;

void main()
{
    const vec4 localPos = vec4(inPos, 1.0);
    const vec4 worldPos = uboCamera.model * localPos;
    outWorldPos = worldPos.xyz;

    // ========== 核心性能优化：避开昂贵 inverse(model) ==========
    // 提取模型矩阵左上角3x3旋转缩放子矩阵
    const mat3 modelRotScale = mat3(uboCamera.model);
    // 计算缩放向量，用于判断是否均匀缩放
    const vec3 scale = vec3(
        length(modelRotScale[0]),
        length(modelRotScale[1]),
        length(modelRotScale[2])
    );
    // 均匀缩放阈值误差
    const float scaleEps = 1e-4;
    const bool uniformScale = all(lessThan(abs(scale - scale.xxx), vec3(scaleEps)));

    vec3 worldNrm;
    if (uniformScale)
    {
        // 均匀缩放：法线直接乘旋转矩阵，无需求逆转置，性能暴涨
        worldNrm = modelRotScale * inNormal;
    }
    else
    {
        // 非均匀缩放才走逆矩阵，仅必要时执行高开销运算
        const mat3 normalMat = transpose(inverse(modelRotScale));
        worldNrm = normalMat * inNormal;
    }

    // ========== 无分支 NaN 防护，完全去掉 if 分支（GPU分支会拖慢并行） ==========
    const float lenNrm = length(worldNrm);
    // 长度过小则替换为向上默认法线，step无分支运算
    const vec3 safeNrm = mix(vec3(0, 1, 0), worldNrm, step(1e-6, lenNrm));
    // 防止除0：极小值兜底，避免 normalize 产生NaN
    const float safeLen = max(lenNrm, 1e-6);
    outNormal = safeNrm / safeLen;

    // 直接传递原始UV与顶点色，无额外计算
    outUV = inUV;
    outVertColor = inColor;

    // Vulkan 标准裁剪空间变换，计算顺序固定
    gl_Position = uboCamera.proj * uboCamera.view * worldPos;
}