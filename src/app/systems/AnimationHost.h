#pragma once
// 阶段 3c：动画子系统（自 Application 拆出）。
// 职责：动画状态机构建（Idle/Walk/Jump + 参数/过渡）与每帧推进（参数写入/
//       过渡评估/采样），以及 glTF 根节点 TRS 增量 → 实例矩阵偏移（驱动
//       网格空间烘焙顶点的动画）。
// 每帧输入（角色速度/着地/是否激活）由 Application 以参数传入，不依赖物理引擎；
// glTF 模型数据由 Application 经 BindModel/模型引用注入。

#include "scene/Animation.h"
#include "scene/AnimationStateMachine.h"

#include <glm/glm.hpp>

#include <memory>
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

    // A1 动画事件派发（生产接线）：事件播放器在主 clip 上循环推进，
    // 每帧由 Update() 驱动；调用方 DrainFiredEvents() 取走本帧触发事件并清空缓冲。
    // 无 glTF 动画时返回空。事件名由内置轨约定（见 UpdateEventPlayer）。
    [[nodiscard]] std::vector<Scene::AnimationEvent> DrainFiredEvents() noexcept
    {
        return std::move(firedEvents_);
    }

    // A1 演示钩子（--demo-events）：设置内置轨使用的事件名。空串（默认）时沿用生产约定
    // 名 "tick"（单测 AnimEvents.HostWiring 依赖）；非空时（如 "click"）由该名驱动内置轨，
    // 使 Application 侧 "click"->SfxId::Click 映射在无动画资产时也可在运行期被观察。
    // 须在事件播放器首次构建（首次 Update / simulating 首帧）之前调用。
    void SetDemoEventName(std::string name) { demoEventName_ = std::move(name); }

  private:
    // 根节点动画 TRS 相对绑定姿态的增量 → T*R*S 前置矩阵（原 UpdateGltfAnimationPose）
    void UpdateGltfOffset(const Scene::GltfModel& gltfModel, bool hasGltf);
    // A1：驱动循环事件播放器并把本帧触发事件存入 firedEvents_。
    // 绑定主 clip（animIndex 0），首次/模型变化时重建并挂内置轨。
    void UpdateEventPlayer(const Scene::GltfModel& model, bool hasGltf, float dt);

    Scene::AnimationStateMachine sm_;
    bool inited_ = false;

    // 采样输出（SamplePose 每帧复用）
    std::vector<glm::vec3> poseT_;
    std::vector<glm::quat> poseR_;
    std::vector<glm::vec3> poseS_;
    glm::mat4 gltfOffset_{1.0f}; // 当前根节点动画增量（无动画时为单位阵）

    // A1 动画事件播放器（主 clip 循环推进）。模型为 Application 侧常驻成员，
    // 其地址与生命周期稳定；模型内容重载（成功/失败）由 hasGltf 与 clip 名检测触发重建。
    std::unique_ptr<Scene::AnimationEventPlayer> eventPlayer_;
    Scene::AnimationEventTrack builtinTrack_;       // 内置事件轨（成员，地址稳定供 BindTrack）
    std::vector<Scene::AnimationEvent> firedEvents_; // 本帧触发事件缓冲
    size_t eventAnimIndex_ = size_t(-1);            // 当前播放器绑定的 clip 下标
    std::string eventClipName_;                     // 当前 clip 名（检测重载）
    std::string demoEventName_;                     // 非空 = 内置轨改用此事件名（演示钩子，默认 "tick"）
};
} // namespace BigHero
