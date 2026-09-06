#pragma once
// 动画系统：骨骼动画状态机 + AnimationBlender
// 原 Application 中 animStateMachine_, AnimationState/Blender 相关逻辑

#include "ISubSystem.h"
#include "scene/Animation.h"
#include "scene/AnimationStateMachine.h"

namespace BigHero::App
{

class AnimationSystem final : public ISubSystem
{
public:
    AnimationSystem() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "AnimationSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -10; }

    void Init()
    {
        animStateMachine_ = Scene::AnimationStateMachine();
    }

    void Shutdown() override {}

    void Update(const FrameContext& frame) override
    {
        animStateMachine_.Update(frame.deltaTime);
    }

    void PreRender(uint32_t) override {}
    void OnSwapchainRecreated() override {}
    void OnRenderPassRecreated() override {}

    // 设置角色速度/着地/跳跃参数（驱动状态机转换）
    void SetCharacterParams(float speed, bool grounded, bool jump)
    {
        animStateMachine_.SetSpeed(speed);
        animStateMachine_.SetGrounded(grounded);
        animStateMachine_.SetJump(jump);
    }

    // 获取当前动画姿态（用于 GPU Skinning）
    void GetCurrentPose(std::vector<glm::vec3>& T, std::vector<glm::quat>& R, std::vector<glm::vec3>& S)
    {
        animStateMachine_.GetPose(T, R, S);
    }

    [[nodiscard]] const Scene::AnimationStateMachine& StateMachine() const noexcept { return animStateMachine_; }
    [[nodiscard]] Scene::AnimationStateMachine& StateMachine() noexcept { return animStateMachine_; }

    [[nodiscard]] const char* Name() const noexcept override { return "AnimationSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -10; }

private:
    Scene::AnimationStateMachine animStateMachine_;
};

} // namespace BigHero::App