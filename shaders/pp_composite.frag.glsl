#version 450
// 合成 Pass：将 HDR 场景颜色与模糊亮部相加，执行 ACES 色调映射，输出到交换链。
// 升级 25：体积雾——合成端光线步进高度雾（指数高度密度 + HG 前向散射）。
// 升级 27：雾效阴影采样——步进点投影 CSM 图集，阴影处削减太阳散射，丁达尔光柱自然涌现；
//          步数运行时可调（16/32/64），配合像素抖动去条带。
// 升级 26：自动曝光（采样 1x1 适应亮度图）+ 暗角 + 胶片颗粒。
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom;
layout(set = 0, binding = 2) uniform sampler2D uLinearDepth;   // R32F 正向视线距离（米）
layout(set = 0, binding = 3) uniform sampler2D uAdaptedLum;    // 1x1 平均对数亮度（自动曝光）
// 升级 27：雾效阴影采样复用场景级联数据与 CSM 深度图集（CPU 端每帧绑定同源资源）
layout(set = 0, binding = 4, std140) uniform FogLightUBO
{
    vec3 lightDir;
    float dirIntensity;
    vec3 lightColor;
    float ambientFactor;
    vec3 cameraPos;
    float pointLightCount;
    float shadowStrength;
    float shadowBias;
    float iblStrength;
    float exposure;
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
    vec4 cameraForward; // xyz=相机前向单位向量，w=阴影最远绘制距离
    vec4 padLights[24]; // GpuPointLight lights[8] 占位（8×48B=384B，不采样点光源，仅保持偏移一致）
} lightUbo;

layout(set = 0, binding = 5) uniform sampler2D uFogShadowMap; // CSM 2x2 深度图集

layout(push_constant) uniform CompositeParams
{
    float bloomStrength;    // 0
    float exposure;         // 4  手动曝光；自动模式下作为补偿系数相乘
    float saturation;       // 8
    float contrast;         // 12
    float lift;             // 16
    float gain;             // 20
    float gamma;            // 24
    float fogQuality;       // 28  体积雾步数(16/32/64)；小数 0.5 分量=启用雾中投影；0=关闭雾
    float fogDensity;       // 32  基准高度处雾密度
    float fogHeightFalloff; // 36  高度指数衰减率
    float fogBaseHeight;    // 40  基准高度（米），其下密度饱和
    float fogScatter;       // 44  阳光前向散射强度混合系数 [0,1]
    float tanHalfFov;       // 48
    float aspect;           // 52
    float autoExposure;     // 56  1=启用自动曝光
    float keyValue;         // 60  中灰键值（平均亮度映射目标）
    vec3 fogTint; float vignetteIntensity; // 64  雾散射染色（HDR 相乘）/ 暗角强度
    vec3 camPos;  float vignetteRadius;    // 80 / 暗角起始半径 [0,1]
    vec3 sunL;    float grainAmount;       // 96  指向太阳的单位向量 / 颗粒强度
    vec3 camFwd;  float grainTime;         // 112 / 颗粒动画时间（秒）
} params;

// ACES 电影级色调映射（Narkowicz 近似）
vec3 acesTonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// 色调分级（与 CPU render/ColorGrading.h 的 GradeColor 同一公式）
vec3 gradeColor(vec3 c)
{
    vec3 x = c * params.gain + vec3(params.lift);
    x = max(x, vec3(0.0));
    x = pow(x, vec3(params.gamma));
    x = (x - vec3(0.5)) * params.contrast + vec3(0.5);
    float luma = dot(x, vec3(0.2126, 0.7152, 0.0722));
    x = mix(vec3(luma), x, params.saturation);
    return max(x, vec3(0.0));
}

// ---- 体积雾：从相机到片元世界位置的射线步进，累积雾消光与内散射 ----
// 步数编码在 push constant 中（16/32/64），小数 0.5 分量表示启用雾中投影
const int kMaxFogSteps = 64;

// 升级 27：CSM 阴影采样（2x2 PCF，无级间混合——雾本身连续，无需软过渡）
// 与 frag.glsl pcfCascade 同源数学：光视投影 → 图集子块 → 深度比较
float fogShadowFactor(vec3 worldPos)
{
    const float viewDepth = dot(lightUbo.cameraForward.xyz, worldPos - lightUbo.cameraPos);
    if (viewDepth >= lightUbo.cameraForward.w)
        return 1.0; // 超出阴影绘制距离

    int cascade = 3;
    for (int i = 0; i < 4; ++i)
    {
        if (viewDepth <= lightUbo.cascadeSplits[i])
        {
            cascade = i;
            break;
        }
    }

    const vec4 lp = lightUbo.lightSpaceMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;

    const vec2 tileOffset = vec2(float(cascade & 1), float(cascade / 2)) * 0.5;
    proj.xy = proj.xy * 0.5 + tileOffset;

    // 光照视锥之外（z 越界或溢出子块覆盖）不受阴影
    if (proj.z >= 1.0 ||
        any(lessThan(proj.xy, tileOffset)) || any(greaterThan(proj.xy, tileOffset + vec2(0.5))))
        return 1.0;

    // 步进点无表面法线，偏移取固定较大值防地面自遮挡痤疮
    const float bias = max(lightUbo.shadowBias, 1e-4) * 2.0;

    const vec2 texelSize = 1.0 / vec2(textureSize(uFogShadowMap, 0));
    float shadow = 0.0;
    for (int x = 0; x < 2; ++x)
    {
        for (int y = 0; y < 2; ++y)
        {
            const vec2 uv = proj.xy + (vec2(x, y) - 0.5) * texelSize * 1.5;
            const float sampled = texture(uFogShadowMap, uv).r;
            shadow += (proj.z - bias > sampled) ? 0.0 : 1.0;
        }
    }
    return shadow * 0.25;
}

vec3 applyVolumetricFog(vec3 color)
{
    // 步数解码：整数部分 = 步数，小数 0.5 = 雾中投影开关
    const float fq = max(params.fogQuality, 0.0);
    const int kSteps = int(floor(fq + 0.25));
    const bool shadowOn = fract(fq) > 0.25;

    // 屏幕射线方向：fwd + right*x*tan*aspect + up*y*tan
    // right/up 由 camFwd 与世界上向量重建（天顶附近回退 Z 轴，避免叉积退化）
    const vec2 ndc = inUv * 2.0 - 1.0;
    const vec3 helper = (abs(params.camFwd.y) > 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    const vec3 right = normalize(cross(params.camFwd, helper));
    const vec3 up = cross(right, params.camFwd);
    const vec3 rd = normalize(params.camFwd + right * (ndc.x * params.tanHalfFov * params.aspect) +
                              up * (ndc.y * params.tanHalfFov));

    // 射线终点：线性深度为沿视线轴距离，换算到射线长度
    const float zView = texture(uLinearDepth, inUv).r;
    const float tEnd = zView / max(dot(rd, params.camFwd), 1e-3);
    if (tEnd <= 0.05)
        return color;

    // 像素哈希抖动起点，隐藏低步数条带
    const float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);

    const float dt = tEnd / float(kSteps);
    float transmittance = 1.0;
    vec3 scatter = vec3(0.0);
    float t = dt * (0.5 + dither);
    for (int i = 0; i < kSteps && i < kMaxFogSteps; ++i)
    {
        const vec3 p = params.camPos + rd * t;
        const float h = max(p.y - params.fogBaseHeight, 0.0);
        // 指数高度雾密度：基准高度处 fogDensity，向上指数衰减，其下饱和
        const float rho = params.fogDensity * exp(-h * params.fogHeightFalloff);
        const float sigma = rho * dt;
        // Henyey-Greenstein 相函数（g=0.55 前向散射峰）+ 少量常数环境散射
        const float g = 0.55;
        const float hg = (1.0 - g * g) / (12.566371 * pow(1.0 + g * g - 2.0 * g * dot(rd, params.sunL), 1.5));
        const float phase = mix(0.08, hg * 12.566371, params.fogScatter);
        // 升级 27：步进点投影 CSM，阴影中仅保留 6% 环境散射——遮挡体在雾中投出光柱
        float sunVis = 1.0;
        if (shadowOn)
            sunVis = mix(0.06, 1.0, fogShadowFactor(p));
        scatter += params.fogTint * (phase * sunVis) * transmittance * sigma;
        transmittance *= exp(-sigma);
        t += dt;
    }
    return color * transmittance + scatter;
}

void main()
{
    vec3 scene = texture(uScene, inUv).rgb;
    vec3 bloom = texture(uBloom, inUv).rgb;

    // 曝光：自动模式用适应后的平均亮度反推曝光（键值/均值，钳制防极端），手动模式直用系数
    float exposureValue = params.exposure;
    if (params.autoExposure > 0.5)
    {
        const float avgLogLum = texture(uAdaptedLum, vec2(0.5)).r;
        const float avgLum = clamp(exp(avgLogLum), 1e-4, 100.0);
        exposureValue = params.exposure * params.keyValue / avgLum;
        exposureValue = clamp(exposureValue, 0.05, 16.0);
    }

    // 体积雾作用于场景颜色（Bloom 之前），雾散射自身为 HDR 亮度，参与曝光/ACES
    vec3 color = (params.fogQuality > 0.25) ? applyVolumetricFog(scene) : scene;
    color += bloom * params.bloomStrength;
    color *= exposureValue;
    color = acesTonemap(color);

    // 升级 21：色调分级（在 ACES 之后作用于显示参考颜色）
    color = gradeColor(color);

    // 升级 26：暗角（显示参考空间，径向平滑衰减；vignetteRadius 为开始变暗的半径）
    if (params.vignetteIntensity > 0.0)
    {
        const float d = length(inUv - 0.5) * 1.4142136; // 0=中心 → 1=角落
        color *= 1.0 - params.vignetteIntensity * smoothstep(params.vignetteRadius, 1.0, d);
    }

    // 升级 26：胶片颗粒（随时间跳动的加性噪声，显示参考空间）
    if (params.grainAmount > 0.0)
    {
        const vec2 seed = gl_FragCoord.xy + vec2(params.grainTime * 17.0, params.grainTime * 31.0);
        const float n = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
        color += (n - 0.5) * params.grainAmount;
    }

    // 伽马校正（交换链为 sRGB 格式时由硬件完成，此处输出线性）
    outColor = vec4(color, 1.0);
}
