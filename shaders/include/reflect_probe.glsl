// ============================================================================
// U2-L2 反射探针采样（前向 frag / 延迟 deferred_light 共用 include）
//
// 与 CPU 侧 src/render/ReflectionProbe.h 逐位同构：
//   - 每探针存原始辐射亮度 SH（未预滤波，9 系数/通道），粗糙度带通衰减
//     f_l(α) = exp(-l(l+1)·α²/2) 在片元施加（α=0 精确锐利）。
//   - box 视差校正（Lagarde）：盒内起点 slab 法，出口 = 各轴 t 的最小者。
//   - 混合权重：三轴三角窗（盒中心 1 / 盒壁 0）跨探针归一化；无盒内探针时
//     回退 IBL（接线 v1 简化：不走 CPU 的「最近中心兜底」，盒外保持 IBL 连续感）。
//   - 返回值 .rgb = 该片元反射方向的烘焙镜面辐射亮度（未乘 F0/BRDF），
//     .a = 探针影响权重（0 = 完全回退 IBL）。
// 所有循环上界均为编译期常量（unrollable），兼容动态均匀索引受限的平台。
// ============================================================================
#ifndef BIG_HERO_REFLECT_PROBE_GLSL
#define BIG_HERO_REFLECT_PROBE_GLSL

struct ReflectProbeGpu
{
    vec4 posValid; // xyz=探针位置, w=1有效 / 0无效
    vec4 boxMin;   // xyz=影响盒 min
    vec4 boxMax;   // xyz=影响盒 max
    vec4 sh[9];    // rgb=SH 系数（原始辐射亮度）, a=未用
};

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_REFLECT_PROBE_UBO, std140) uniform ReflectProbeBlock
{
    vec4 countPad;                                       // x=有效探针数（0=未烘焙）, y=捕获立方图可用（1/0）
    ReflectProbeGpu probes[BH_MATERIAL_REFLECT_PROBE_MAX];
} reflProbeUbo;

// GPU 真实场景捕获立方图（v1：单探针=probes[0]；FinishFrame 已生成 mip 链，roughness→LOD）
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_REFLECT_PROBE_CAPTURE) uniform samplerCube probeCaptureMap;

// SH 实基（∫Y²dω=1 归一化，与 CPU Sh9::Basis 逐位一致）
void reflProbeSHBasis(vec3 dir, out float b[9])
{
    const vec3 d = normalize(length(dir) > 1e-8 ? dir : vec3(0.0, 0.0, 1.0));
    const float x = d.x, y = d.y, z = d.z;
    b[0] = 0.282095;
    b[1] = 0.488603 * y;
    b[2] = 0.488603 * z;
    b[3] = 0.488603 * x;
    b[4] = 1.092548 * x * y;
    b[5] = 1.092548 * y * z;
    b[6] = 0.315392 * (3.0 * z * z - 1.0);
    b[7] = 1.092548 * x * z;
    b[8] = 0.546274 * (x * x - y * y);
}

// box 视差校正（与 CPU ParallaxCorrectedDir 同构：盒内 clamped 起点 + slab 最小 t 出口）
vec3 reflProbeParallaxDir(ReflectProbeGpu p, vec3 worldPos, vec3 r)
{
    const vec3 o = clamp(worldPos, p.boxMin.xyz, p.boxMax.xyz) - p.posValid.xyz;
    if (dot(o, o) < 1e-12)
        return r;
    float tExit = 1e30;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float d = r[axis];
        if (abs(d) <= 1e-5)
            continue;
        const float lo = p.boxMin[axis] - p.posValid[axis];
        const float hi = p.boxMax[axis] - p.posValid[axis];
        const float target = (d >= 0.0) ? hi : lo;
        tExit = min(tExit, (target - o[axis]) / d);
    }
    if (tExit >= 1e30)
        return r;
    const vec3 hit = o + r * max(tExit, 0.0);
    const float cl = length(hit);
    return cl > 1e-8 ? hit / cl : r;
}

// 单探针求值：视差校正方向 + 粗糙度带通衰减 + SH 求值（非负钳制）
vec3 reflProbeEvalOne(ReflectProbeGpu p, vec3 worldPos, vec3 r, float roughness)
{
    const vec3 rd = reflProbeParallaxDir(p, worldPos, r);
    const float a = clamp(roughness, 0.0, 1.0);
    const float f1 = exp(-a * a);   // l=1: exp(-2·α²/2)
    const float f2 = exp(-3.0 * a * a); // l=2: exp(-6·α²/2)
    float b[9];
    reflProbeSHBasis(rd, b);
    vec3 acc = vec3(0.0);
    for (int k = 0; k < 9; ++k)
    {
        float f = 1.0;
        if (k >= 1 && k <= 3)
            f = f1;
        if (k >= 4)
            f = f2;
        acc += p.sh[k].rgb * (b[k] * f);
    }
    return max(acc, vec3(0.0));
}

// 入口：盒内探针三角窗权重归一化；无盒内（或未烘焙/全无效）→ 权重 0（回退 IBL）。
// countPad.y>0.5 时走 GPU 捕获立方图（textureLod 按粗糙度取 mip，视差方向取权重最高
// 探针的校正方向）；否则回退 SH 解析环境求值。两路同享同一权重几何。
// 返回 vec4(rgb=烘焙镜面辐射亮度, a=影响权重)。
vec4 SampleReflectionProbe(vec3 worldPos, vec3 r, float roughness)
{
    const int probeCount = int(reflProbeUbo.countPad.x);
    if (probeCount <= 0)
        return vec4(0.0);
    float wsum = 0.0;
    float w[BH_MATERIAL_REFLECT_PROBE_MAX];
    int bestIdx = -1;
    float bestW = 0.0;
    for (int i = 0; i < BH_MATERIAL_REFLECT_PROBE_MAX; ++i)
    {
        w[i] = 0.0;
        if (i >= probeCount)
            break;
        const ReflectProbeGpu p = reflProbeUbo.probes[i];
        if (p.posValid.w < 0.5)
            continue;
        const vec3 lo = p.boxMin.xyz;
        const vec3 hi = p.boxMax.xyz;
        if (worldPos.x < lo.x || worldPos.x > hi.x || worldPos.y < lo.y || worldPos.y > hi.y ||
            worldPos.z < lo.z || worldPos.z > hi.z)
            continue;
        const vec3 t = clamp((worldPos - lo) / max(hi - lo, vec3(1e-4)), vec3(0.0), vec3(1.0));
        const vec3 tri = 1.0 - abs(t - 0.5) * 2.0;
        w[i] = max(tri.x * tri.y * tri.z, 0.0);
        wsum += w[i];
        if (w[i] > bestW)
        {
            bestW = w[i];
            bestIdx = i;
        }
    }
    if (wsum <= 1e-6)
        return vec4(0.0);

    if (reflProbeUbo.countPad.y > 0.5 && bestIdx >= 0)
    {
        // GPU 捕获路径：真实渲染场景的立方图（含建筑/霓虹/天空），视差校正 + 粗糙度取 mip
        const vec3 rd = reflProbeParallaxDir(reflProbeUbo.probes[bestIdx], worldPos, r);
        const vec3 captured = textureLod(probeCaptureMap, rd, roughness * 4.0).rgb;
        return vec4(captured, min(wsum, 1.0));
    }

    vec3 acc = vec3(0.0);
    for (int i = 0; i < BH_MATERIAL_REFLECT_PROBE_MAX; ++i)
    {
        if (i >= probeCount)
            break;
        const ReflectProbeGpu p = reflProbeUbo.probes[i];
        if (p.posValid.w < 0.5 || w[i] <= 1e-6)
            continue;
        acc += reflProbeEvalOne(p, worldPos, r, roughness) * (w[i] / wsum);
    }
    return vec4(acc, min(wsum, 1.0));
}

#endif // BIG_HERO_REFLECT_PROBE_GLSL