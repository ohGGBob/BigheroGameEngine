#pragma once
// 阶段 3c：动画子系统（自 Application 拆出）。
// 职责：动画状态机构建（Idle/Walk/Jump + 参数/过渡）与每帧推进（参数写入/
//       过渡评估/采样），以及 glTF 根节点 TRS 增量 → 实例矩阵偏移（驱动
//       网格空间烘焙顶点的动画）。
// 每帧输入（角色速度/着地/是否激活）由 Application 以参数传入，不依赖物理引擎；
// glTF 模型数据由 Application 经 BindModel/模型引用注入。

#include "scene/AnimationStateMachine.h"

#include <glm/glm.hpp>
#include <vector>

namespace BigHero
{
class AnimationHost
{
  public:
    // 每帧状态机输入（Application 从物理角色控制器解析后传入）
    struct FrameInput
    {
        bool characterActive = false; // 角色控制器启用且刚体有效
        float speed = 0.0f;           // 角色水平速度（未启用时 0，保持 Idle）
        bool grounded = true;         // 是否着地
    };

    // 状态机构建（原 Application::InitAnimationStateMachine；幂等，重复调用无效）
    void Init();

    // 每帧推进：写入参数 → Update(dt) 过渡评估 → 采样并计算 glTF 根节点增量
    // （原 Application::UpdateAnimationStateMachine + UpdateGltfAnimationPose）
    void Update(float dt, const FrameInput& input, const Scene::GltfModel& gltfModel, bool hasGltf);

    // 编辑器面板直写状态机（暂停/时间轴/状态属性）
    [[nodiscard]] Scene::AnimationStateMachine& StateMachine() noexcept { return sm_; }
    // glTF 根节点动画增量矩阵（实例矩阵前置；无动画时为单位阵）
    [[nodiscard]] const glm::mat4& GltfOffset() const noexcept { return gltfOffset_; }
    // 状态机是否已初始化
    [[nodiscard]] bool IsInitialized() const noexcept { return inited_; }

  private:
    // 根节点动画 TRS 相对绑定姿态的增量 → T*R*S 前置矩阵（原 UpdateGltfAnimationPose）
    void UpdateGltfOffset(const Scene::GltfModel& gltfModel, bool hasGltf);

    Scene::AnimationStateMachine sm_;
    bool inited_ = false;

    // 采样输出（SamplePose 每帧复用）
    std::vector<glm::vec3> poseT_;
    std::vector<glm::quat> poseR_;
    std::vector<glm::vec3> poseS_;
    glm::mat4 gltfOffset_{1.0f}; // 当前根节点动画增量（无动画时为单位阵）
};
} // namespace BigHero
