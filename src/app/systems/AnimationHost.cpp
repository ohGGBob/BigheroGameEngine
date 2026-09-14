#include "app/systems/AnimationHost.h"

#include "core/Log.h"

#include <cmath>

namespace BigHero
{
void AnimationHost::Init()
{
    if (inited_)
        return;
    inited_ = true;

    // 状态：Idle / Walk / Jump（animationIndex=-1 表示绑定姿态，加载 glTF 角色后替换为实际动画下标）
    const int idle = sm_.AddState("Idle", -1, 1.0f, true);
    const int walk = sm_.AddState("Walk", -1, 1.0f, true);
    const int jump = sm_.AddState("Jump", -1, 1.0f, false);

    // 参数：Speed（水平速度）、Grounded（是否着地）、Jump（跳跃触发）
    sm_.SetFloat("Speed", 0.0f);
    sm_.SetBool("Grounded", true);

    // 过渡：Idle <-> Walk（速度阈值）
    sm_.AddTransition(idle, walk, 0.20f, {{"Speed", Scene::AnimConditionType::FloatGreater, 0.5f}});
    sm_.AddTransition(walk, idle, 0.20f, {{"Speed", Scene::AnimConditionType::FloatLess, 0.5f}});

    // 过渡：任意状态 -> Jump（跳跃触发，需着地）
    sm_.AddTransition(-1, jump, 0.15f,
                      {{"Jump", Scene::AnimConditionType::Trigger}, {"Grounded", Scene::AnimConditionType::BoolTrue}});

    // 过渡：Jump -> Idle（着地后，带退出时间确保跳跃动画播放一段）
    sm_.AddTransitionWithExit(jump, idle, 0.25f, 0.3f, {{"Grounded", Scene::AnimConditionType::BoolTrue}});

    sm_.SetInitialState(idle);
    LOG_INFO("动画状态机初始化: " << sm_.StateCount() << " 状态, " << sm_.TransitionCount() << " 过渡");
}

void AnimationHost::Update(float dt, const FrameInput& input, const Scene::GltfModel& gltfModel, bool hasGltf)
{
    if (!inited_)
        Init();

    // 角色启用时：物理刚体水平速度/着地状态作为参数；否则保持 Idle 默认
    if (input.characterActive)
    {
        sm_.SetFloat("Speed", input.speed);
        sm_.SetBool("Grounded", input.grounded);
    }
    else
    {
        sm_.SetFloat("Speed", 0.0f);
        sm_.SetBool("Grounded", true);
    }

    sm_.Update(dt);
    UpdateGltfOffset(gltfModel, hasGltf);
}

void AnimationHost::UpdateGltfOffset(const Scene::GltfModel& gltfModel, bool hasGltf)
{
    gltfOffset_ = glm::mat4(1.0f);
    if (!hasGltf || gltfModel.animations.empty() || gltfModel.nodeParents.empty())
        return;

    const int cs = sm_.CurrentState();
    if (cs < 0)
        return;
    const int animIdx = sm_.GetState(cs).animationIndex;
    if (animIdx < 0 || animIdx >= static_cast<int>(gltfModel.animations.size()))
        return;

    // 采样当前状态（含 crossfade 混合）的节点局部 TRS
    sm_.SamplePose(gltfModel, poseT_, poseR_, poseS_);

    // 取该动画涉及的第一个根节点（parent == -1）：顶点按网格空间烘焙，
    // 仅根节点 TRS 能以实例矩阵增量形式驱动渲染；非根节点通道只影响状态机预览。
    const Scene::GltfAnimation& anim = gltfModel.animations[static_cast<size_t>(animIdx)];
    int root = -1;
    for (const Scene::GltfAnimationChannel& ch : anim.channels)
    {
        const size_t node = static_cast<size_t>(ch.targetNode);
        if (ch.targetNode >= 0 && node < gltfModel.nodeParents.size() && gltfModel.nodeParents[node] == -1)
        {
            root = ch.targetNode;
            break;
        }
    }
    if (root < 0)
        return;

    // 根节点动画 TRS 相对绑定姿态的增量 → T*R*S 前置矩阵
    const glm::vec3 dT = poseT_[static_cast<size_t>(root)] - gltfModel.nodeTranslations[static_cast<size_t>(root)];
    const glm::quat dR =
        poseR_[static_cast<size_t>(root)] * glm::inverse(gltfModel.nodeRotations[static_cast<size_t>(root)]);
    const glm::vec3 bindS = gltfModel.nodeScales[static_cast<size_t>(root)];
    const glm::vec3 dS(poseS_[static_cast<size_t>(root)] / glm::max(bindS, glm::vec3(1e-4f)));

    gltfOffset_ = glm::translate(glm::mat4(1.0f), dT) * glm::mat4_cast(dR) * glm::scale(glm::mat4(1.0f), dS);
}
} // namespace BigHero
