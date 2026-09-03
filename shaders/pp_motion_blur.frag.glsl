#version 450
// 屏幕空间相机运动模糊（Camera Motion Blur）：
// 利用当前帧 MSAA 深度重建 NDC 坐标，用重投影矩阵（prevVP × inverse(currVP)）
// 把当前像素映射到上一帧屏幕 UV，得到速度向量，沿轨迹方向累积多次采样，
// 形成运动拖尾。enabled=0 时直通原图（不改变画面观感）。
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;   // HDR 场景颜色（已 resolve）
layout(set = 0, binding = 1) uniform sampler2DMS sceneDepth;  // MSAA 深度（采样还原 NDC z）

// 重投影矩阵 = prevVP × inverse(currVP)，直接把当前裁剪坐标映射到上一帧裁剪坐标
layout(push_constant) uniform Params
{
    mat4  reproj;     // 64B：prevVP × inverse(currVP)
    float strength;   // 速度缩放 [0,1]
    float maxBlur;    // 速度向量长度上限（UV 空间，限速防全屏涂抹）
    float enabled;    // 1=启用，0=直通
    float maxSamples; // 采样数（float 形式，避免额外 uniform）
} params;

void main()
{
    vec3 center = texture(sceneColor, inUv).rgb;
    if (params.enabled < 0.5)
    {
        outColor = vec4(center, 1.0);
        return;
    }

    // ---- 重建当前像素的 NDC 深度（Vulkan z ∈ [0,1]），逐 MSAA 样本取均值 ----
    ivec2 coord = ivec2(gl_FragCoord.xy);
    int samples = textureSamples(sceneDepth);
    float depth = 0.0;
    for (int i = 0; i < samples; ++i)
        depth += texelFetch(sceneDepth, coord, i).r;
    depth /= float(samples);

    // ---- 当前裁剪坐标 → 上一帧裁剪坐标（重投影矩阵已含 inverse(currVP)）----
    vec4 clip = vec4(inUv * 2.0 - 1.0, depth, 1.0);
    vec4 prevClip = params.reproj * clip;
    vec2 prevUv = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // ---- 速度向量（当前位置指向其在上一帧的位置）----
    vec2 velocity = inUv - prevUv;

    // 限速：相机大幅跳变时避免整屏涂抹
    float vlen = length(velocity);
    if (vlen > params.maxBlur)
        velocity *= params.maxBlur / vlen;

    // ---- 沿速度方向累积采样，构建运动轨迹拖尾 ----
    int n = int(params.maxSamples + 0.5);
    vec3 acc = center;
    float wsum = 1.0;
    for (int i = 1; i <= n; ++i)
    {
        float t = float(i) / float(n);
        vec2 uv = inUv - velocity * t * params.strength;
        acc += texture(sceneColor, clamp(uv, 0.0, 1.0)).rgb;
        wsum += 1.0;
    }

    outColor = vec4(acc / wsum, 1.0);
}
