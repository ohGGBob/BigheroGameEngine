#pragma once
// 后处理管理：Bloom + 色调映射 + 色调分级 + 景深 + 运动模糊
// 原 Application 中 gradeSaturation_/Contrast_/Lift_/Gain_/Gamma_ 等逻辑

#include "ISubSystem.h"
#include "render/PostProcessor.h"
#include "render/ColorGrading.h"

namespace BigHero::App
{

class PostProcessManager final : public ISubSystem
{
public:
    PostProcessManager() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "PostProcessManager"; }
    [[nodiscard]] int Priority() const noexcept override { return 30; }

    void Init(const Context& ctx, const Renderer& renderer)
    {
        ctx_ = &ctx;
        renderer_ = &renderer;
    }

    void Shutdown() override {}

    void Update(const FrameContext& frame) override {}

    void PreRender(uint32_t frameIndex) override {}

    void OnSwapchainRecreated() override {}

    void OnRenderPassRecreated() override {}

    // 每帧更新后处理参数
    void SetCameraParams(float nearZ, float farZ)
    {
        renderer_->SetPostProcessingCamera(nearZ, farZ);
    }

    void SetMotionBlurCamera(const glm::mat4& prevVP, const glm::mat4& currVP)
    {
        renderer_->SetMotionBlurCamera(prevVP, currVP);
    }

    // 配置访问
    void SetEnabled(bool v) { enabled_ = v; }
    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }

    void SetBloomEnabled(bool v) { bloomEnabled_ = v; }
    [[nodiscard]] bool BloomEnabled() const noexcept { return bloomEnabled_; }

    void SetDeferred(bool v) { deferredEnabled_ = v; }
    [[nodiscard]] bool DeferredEnabled() const noexcept { return deferredEnabled_; }

    // 色调分级参数
    void SetGradeSaturation(float v) { gradeSaturation_ = v; }
    void SetGradeContrast(float v) { gradeContrast_ = v; }
    void SetGradeLift(float v) { gradeLift_ = v; }
    void SetGradeGain(float v) { gradeGain_ = v; }
    void SetGradeGamma(float v) { gradeGamma_ = v; }
    [[nodiscard]] float GradeSaturation() const noexcept { return gradeSaturation_; }
    [[nodiscard]] float GradeContrast() const noexcept { return gradeContrast_; }
    [[nodiscard]] float GradeLift() const noexcept { return gradeLift_; }
    [[nodiscard]] float GradeGain() const noexcept { return gradeGain_; }
    [[nodiscard]] float GradeGamma() const noexcept { return gradeGamma_; }

    // 景深参数
    void SetDOFEnabled(bool v) { dofEnabled_ = v; }
    void SetDOFFocusDistance(float v) { dofFocusDistance_ = v; }
    void SetDOFAperture(float v) { dofAperture_ = v; }
    void SetDOFMaxBlur(float v) { dofMaxBlur_ = v; }
    [[nodiscard]] bool DOFEnabled() const noexcept { return dofEnabled_; }
    [[nodiscard]] float DOFFocusDistance() const noexcept { return dofFocusDistance_; }
    [[nodiscard]] float DOFAperture() const noexcept { return dofAperture_; }
    [[nodiscard]] float DOFMaxBlur() const noexcept { return dofMaxBlur_; }

    // 运动模糊参数
    void SetMBEnabled(bool v) { mbEnabled_ = v; }
    void SetMBStrength(float v) { mbStrength_ = v; }
    void SetMBMaxBlur(float v) { mbMaxBlur_ = v; }
    void SetMBMaxSamples(float v) { mbMaxSamples_ = v; }
    [[nodiscard]] bool MBEnabled() const noexcept { return mbEnabled_; }
    [[nodiscard]] float MBStrength() const noexcept { return mbStrength_; }
    [[nodiscard]] float MBMaxBlur() const noexcept { return mbMaxBlur_; }
    [[nodiscard]] float MBMaxSamples() const noexcept { return mbMaxSamples_; }

    // SSAO/SSR
    void SetSSAOEnabled(bool v) { ssaoEnabled_ = v; }
    [[nodiscard]] bool SSAOEnabled() const noexcept { return ssaoEnabled_; }
    void SetSSREnabled(bool v) { ssrEnabled_ = v; }
    [[nodiscard]] bool SSREnabled() const noexcept { return ssrEnabled_; }

    // 每帧应用到 PostProcessor
    void ApplyToPostProcessor()
    {
        if (auto* pp = renderer_->GetPostProcessor())
        {
            pp->SetGradeParams(gradeSaturation_, gradeContrast_, gradeLift_, gradeGain_, gradeGamma_);
            pp->SetDOFParams(dofEnabled_, dofFocusDistance_, dofAperture_, dofMaxBlur_);
            pp->SetMBParams(mbEnabled_, mbStrength_, mbMaxBlur_, mbMaxSamples_);
            pp->SetSSAOEnabled(ssaoEnabled_);
            pp->SetSSREnabled(ssrEnabled_);
        }
    }

    [[nodiscard]] const char* Name() const noexcept override { return "PostProcessManager"; }
    [[nodiscard]] int Priority() const noexcept override { return 30; }

private:
    const Renderer* renderer_ = nullptr;

    // 基础开关
    bool enabled_ = false;
    bool bloomEnabled_ = false;
    bool deferredEnabled_ = false;

    // 色调分级
    float gradeSaturation_ = 1.0f;
    float gradeContrast_ = 1.0f;
    float gradeLift_ = 0.0f;
    float gradeGain_ = 1.0f;
    float gradeGamma_ = 1.0f;

    // 景深
    bool dofEnabled_ = false;
    float dofFocusDistance_ = 7.0f;
    float dofAperture_ = 0.03f;
    float dofMaxBlur_ = 0.020f;

    // 运动模糊
    bool mbEnabled_ = false;
    float mbStrength_ = 0.5f;
    float mbMaxBlur_ = 0.02f;
    float mbMaxSamples_ = 16.0f;

    // SSAO/SSR
    bool ssaoEnabled_ = false;
    bool ssrEnabled_ = false;
};

} // namespace BigHero::App