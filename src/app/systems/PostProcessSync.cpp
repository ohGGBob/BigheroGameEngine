#include "app/systems/PostProcessSync.h"

#include "render/PostProcessor.h"

namespace BigHero
{
namespace
{
// Halton 低差异序列（TAA 抖动采样用；自 Application 随 AdvanceJitter 迁入）
float Halton(uint32_t index, uint32_t base)
{
    float f = 1.0f, r = 0.0f;
    while (index > 0)
    {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}
} // namespace

void PostProcessSync::SyncToPostProcessor(Render::PostProcessor* pp, VkExtent2D extent, VkBuffer cascadeUbo,
                                          VkImageView cascadeView, VkSampler cascadeSampler,
                                          const LightParams& light, const OrbitCamera& camera, float dt)
{
    if (pp == nullptr)
        return;

    // 升级 21：把编辑器色调分级参数同步进 PostProcessor（合成阶段每帧读取，作用于 ACES 之后）
    pp->gradeSaturation = gradeSaturation;
    pp->gradeContrast = gradeContrast;
    pp->gradeLift = gradeLift;
    pp->gradeGain = gradeGain;
    pp->gradeGamma = gradeGamma;
    // 升级 22：景深参数同步进 PostProcessor（景深 Pass 每帧读取）
    pp->dofEnabled = dofEnabled;
    pp->dofFocusDistance = dofFocusDistance;
    pp->dofAperture = dofAperture;
    pp->dofMaxBlur = dofMaxBlur;
    // 升级 23：运动模糊参数同步进 PostProcessor（运动模糊 Pass 每帧读取）
    pp->mbEnabled = mbEnabled;
    pp->mbStrength = mbStrength;
    pp->mbMaxBlur = mbMaxBlur;
    pp->mbMaxSamples = mbMaxSamples;
    // 升级 25：体积雾参数同步进 PostProcessor（合成 Pass 光线步进每帧读取）
    pp->fogEnabled = fogEnabled;
    pp->fogDensity = fogDensity;
    pp->fogHeightFalloff = fogHeightFalloff;
    pp->fogBaseHeight = fogBaseHeight;
    pp->fogScatter = fogScatter;
    pp->fogTint = fogTint;
    pp->fogShadowEnabled = fogShadowEnabled; // 升级 27：雾中投影（God Rays）
    pp->fogSteps = fogSteps;
    // 升级 25：体积雾相机环境（相机位置/前向 + 指向太阳向量 + 投影参数）
    // 光照约定与 frag.glsl 一致：lightDir 为光的行进方向，指向太阳取其反向
    const glm::vec3 sunL = -glm::normalize(light.direction);
    const glm::vec3 camFwd = glm::normalize(camera.Target() - camera.Position());
    const float aspect =
        (extent.height > 0) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
    pp->SetFogCamera(camera.Position(), camFwd, sunL, glm::tan(glm::radians(camera.fovDegrees_) * 0.5f), aspect);
    // 升级 27：雾阴影同源资源（级联 UBO 各帧同步同值，任取一帧即可；CSM 图集视图/采样器）
    pp->SetFogShadowResources(cascadeUbo, cascadeView, cascadeSampler);
    // 升级 26：自动曝光 + 电影化参数同步进 PostProcessor（亮度适应/合成 Pass 每帧读取）
    pp->SetDeltaTime(dt);
    pp->autoExposure = autoExposure;
    pp->exposureKeyValue = exposureKeyValue;
    pp->adaptationSpeed = adaptationSpeed;
    pp->vignetteIntensity = vignetteIntensity;
    pp->vignetteRadius = vignetteRadius;
    pp->filmGrain = filmGrain;
    // 曝光同步（0.17.9）：前向片元端不再乘 exposure（输出线性 HDR），曝光统一由
    // pp_composite 应用——必须把光照参数的曝光传进来，否则前向曝光滑条失效
    pp->exposure = light.exposure;
    // 升级 28：TAA 参数同步（开关切换边沿重置历史，下一帧直通重建防拖影）
    pp->taaEnabled = taaEnabled;
    pp->taaFeedback = taaFeedback;
    if (taaEnabled != prevTaaEnabled)
    {
        pp->ResetTaa();
        prevTaaEnabled = taaEnabled;
    }
}

void PostProcessSync::AdvanceJitter(bool jitterActive, VkExtent2D frameExtent, OrbitCamera& camera)
{
    // 升级 28：TAA Halton(2,3) 8 相位循环抖动（±0.5 像素 → NDC），注入本帧投影矩阵；
    // 仅前向 MSAA 路径可用（TAA Pass 依赖 MSAA 深度重建射线）
    if (jitterActive)
    {
        taaJitterPhase = (taaJitterPhase + 1) % 8;
        const float hx = Halton(static_cast<uint32_t>(taaJitterPhase) + 1, 2) - 0.5f;
        const float hy = Halton(static_cast<uint32_t>(taaJitterPhase) + 1, 3) - 0.5f;
        taaJitterX = hx * 2.0f / static_cast<float>(frameExtent.width);
        taaJitterY = hy * 2.0f / static_cast<float>(frameExtent.height);
    }
    else
    {
        taaJitterX = taaJitterY = 0.0f;
        taaJitterPhase = 0;
    }
    camera.SetJitter(taaJitterX, taaJitterY);
}
} // namespace BigHero
