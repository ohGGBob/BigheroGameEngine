#version 450

#include "include/bindings.glsl"
// TAA 时间抗锯齿（升级 28）：
// 1) 用当前帧 MSAA 深度 + 重投影矩阵（prevVP × inverse(currVP)，与运动模糊同源）
//    把当前像素映射到上一帧屏幕 UV，取历史颜色；
// 2) 3x3 邻域在 YCoCg 空间构建 AABB 钳制历史，抑制 ghosting 与色彩偏移；
// 3) 与当前帧颜色自适应混合，累积抖动采样，收敛几何边缘与体积雾抖动颗粒。
// 配套 CPU 端 Halton(2,3) 8 相位子像素抖动投影（仅场景渲染路径）。
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = BH_SET_POST, binding = BH_PP_SLOT0) uniform sampler2D uCurrent; // 本帧场景颜色（后运动模糊）
layout(set = BH_SET_POST, binding = BH_PP_SLOT1) uniform sampler2D uHistory; // 上一帧 TAA 输出（ping-pong）
layout(set = BH_SET_POST, binding = BH_PP_SLOT2) uniform sampler2DMS uDepth; // MSAA 深度（重建 NDC z）

layout(push_constant) uniform Params
{
    mat4 reproj;    // 64B：prevVP × inverse(currVP)（含双方抖动）
    float enabled;  // 1=启用（0=直通，不写历史外参考）
    float feedback; // 历史权重 [0,0.95]
    float newFrame; // 1=首帧/重置（历史无效，直通当前）
    float jitterX;  // 当前帧裁剪空间抖动量（重投影射线重建用）
    float jitterY;
    float pad0;
} params;

// YCoCg 变换：色度/亮度分离，AABB 钳制在 YCoCg 空间做可减少色彩偏移
vec3 rgbToYCoCg(vec3 c)
{
    return vec3(0.299 * c.r + 0.587 * c.g + 0.114 * c.b, 0.5 * c.b - 0.5 * c.r + 0.25 * c.g + 0.5,
                0.5 * c.r - 0.5 * c.b + 0.5);
}

vec3 yCoCgToRgb(vec3 c)
{
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

void main()
{
    const vec3 cur = texture(uCurrent, inUv).rgb;
    if (params.enabled < 0.5 || params.newFrame > 0.5)
    {
        outColor = vec4(cur, 1.0);
        return;
    }

    // ---- 深度重投影（与 pp_motion_blur 同源数学）：当前 NDC → 上一帧 UV ----
    const ivec2 coord = ivec2(gl_FragCoord.xy);
    const int samples = textureSamples(uDepth);
    float depth = 0.0;
    for (int i = 0; i < samples; ++i)
        depth += texelFetch(uDepth, coord, i).r;
    depth /= float(samples);

    // 当前像素沿带抖动的射线渲染：NDC 加回本帧抖动量还原真实射线位置
    const vec2 ndc = inUv * 2.0 - 1.0 + vec2(params.jitterX, params.jitterY);
    const vec4 prevClip = params.reproj * vec4(ndc, depth, 1.0);
    const vec2 prevUv = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // 历史出屏 → 完全使用当前帧（无历史）
    if (any(lessThan(prevUv, vec2(0.0))) || any(greaterThan(prevUv, vec2(1.0))))
    {
        outColor = vec4(cur, 1.0);
        return;
    }
    vec3 history = texture(uHistory, prevUv).rgb;

    // ---- 3x3 邻域均值/方差 → YCoCg AABB（±1.25σ）----
    // textureOffset 要求编译期常量偏移，此处显式展开 9 个固定采样点
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    for (int i = 0; i < 9; ++i)
    {
        // 常量数组下表驱动：(-1,-1)...(1,1)，单层循环亦满足展开
        const ivec2 off = ivec2(i % 3 - 1, i / 3 - 1);
        const vec3 c = rgbToYCoCg(texelFetch(uCurrent, coord + off, 0).rgb);
        m1 += c;
        m2 += c * c;
    }
    m1 /= 9.0;
    m2 /= 9.0;
    const vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));
    const vec3 lo = m1 - sigma * 1.25;
    const vec3 hi = m1 + sigma * 1.25;
    // 历史钳制到邻域盒内：保留亚像素信息，同时把错误历史拉回合法范围
    history = yCoCgToRgb(clamp(rgbToYCoCg(history), lo, hi));

    // ---- 自适应混合：反馈越高越平滑（收敛越快越抗噪），上限 0.95 防过度拖影 ----
    const float alpha = clamp(params.feedback, 0.0, 0.95);
    outColor = vec4(mix(cur, history, alpha), 1.0);
}
