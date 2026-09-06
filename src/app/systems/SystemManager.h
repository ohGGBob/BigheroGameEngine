#pragma once
// 系统管理器：统一管理所有子系统的生命周期和更新顺序
// 替代原 Application 中庞大的成员变量和手动调用

#include "ISubSystem.h"
#include "TimeManager.h"
#include "CameraController.h"
#include "SceneManager.h"
#include "PhysicsSystem.h"
#include "ParticleSystem.h"
#include "NavigationSystem.h"
#include "RenderPipelineManager.h"
#include "AnimationSystem.h"
#include "GizmoSystem.h"
#include "UndoRedoManager.h"
#include "PostProcessManager.h"
#include "EditorIntegration.h"
#include "AudioSystem.h"
#include <memory>
#include <vector>
#include <algorithm>
#include <string>

namespace BigHero::App
{

class SystemManager final
{
public:
    explicit SystemManager(class Window& window, class OrbitCamera& camera, class Editor::Gizmo& gizmo)
        : window_(window), camera_(camera), gizmo_(gizmo),
          timeManager_(std::make_unique<TimeManager>()),
          cameraCtrl_(std::make_unique<CameraController>(window, camera_, gizmo_)),
          sceneMgr_(std::make_unique<SceneManager>()),
          physicsSys_(std::make_unique<PhysicsSystem>()),
          particleSys_(std::make_unique<ParticleSystem>()),
          navSys_(std::make_unique<NavigationSystem>()),
          renderPipelineMgr_(std::make_unique<RenderPipelineManager>()),
          animSys_(std::make_unique<AnimationSystem>()),
          gizmoSys_(std::make_unique<GizmoSystem>(window, gizmo_)),
          undoMgr_(std::make_unique<UndoRedoManager>()),
          postProc_(std::make_unique<PostProcessManager>()),
          editorInt_(std::make_unique<EditorIntegration>()),
          audioSys_(std::make_unique<AudioSystem>())
    {
        // 注册所有子系统
        RegisterSystems();
    }

    ~SystemManager() = default;

    // 初始化所有子系统
    void Init(class Context& ctx, class Renderer& renderer, class Render::DescriptorManager& descManager);

    // 销毁所有子系统
    void Shutdown();

    // 每帧更新所有子系统
    void Update(const FrameContext& frame);

    // 渲染前预处理
    void PreRender(uint32_t frameIndex);

    // 交换链重建回调
    void OnSwapchainRecreated();

    // 渲染通道重建回调
    void OnRenderPassRecreated();

    // UI 录制
    void RecordUI(class VkCommandBuffer cmd, uint32_t imageIndex, class VkExtent2D extent);

    // 访问器
    [[nodiscard]] TimeManager& Time() noexcept { return *timeManager_; }
    [[nodiscard]] CameraController& CameraCtrl() noexcept { return *cameraCtrl_; }
    [[nodiscard]] SceneManager& Scene() noexcept { return *sceneMgr_; }
    [[nodiscard]] PhysicsSystem& Physics() noexcept { return *physicsSys_; }
    [[nodiscard]] ParticleSystem& Particles() noexcept { return *particleSys_; }
    [[nodiscard]] NavigationSystem& Navigation() noexcept { return *navSys_; }
    [[nodiscard]] RenderPipelineManager& Pipelines() noexcept { return *renderPipelineMgr_; }
    [[nodiscard]] AnimationSystem& Animation() noexcept { return *animSys_; }
    [[nodiscard]] GizmoSystem& Gizmo() noexcept { return *gizmoSys_; }
    [[nodiscard]] UndoRedoManager& UndoRedo() noexcept { return *undoMgr_; }
    [[nodiscard]] PostProcessManager& PostProcess() noexcept { return *postProc_; }
    [[nodiscard]] EditorIntegration& Editor() noexcept { return *editorInt_; }
    [[nodiscard]] AudioSystem& Audio() noexcept { return *audioSys_; }

    // 便捷访问常用状态
    [[nodiscard]] float DeltaTime() const noexcept { return timeManager_->DeltaTime(); }
    [[nodiscard]] uint32_t FPS() const noexcept { return timeManager_->LastFps(); }
    [[nodiscard]] class OrbitCamera& Camera() noexcept { return camera_; }
    [[nodiscard]] class Editor::Gizmo& Gizmo() noexcept { return gizmo_; }
    [[nodiscard]] class Window& Window() noexcept { return window_; }

    // 设置 Renderer 指针（用于编辑器更新）
    void SetRenderer(class Renderer* renderer) { renderer_ = renderer; }

private:
    void RegisterSystems();

    class Window& window_;
    class OrbitCamera& camera_;
    class Editor::Gizmo& gizmo_;
    class Renderer* renderer_ = nullptr;

    std::vector<ISubSystem*> systems_;

    // 子系统实例
    std::unique_ptr<TimeManager> timeManager_;
    std::unique_ptr<CameraController> cameraCtrl_;
    std::unique_ptr<SceneManager> sceneMgr_;
    std::unique_ptr<PhysicsSystem> physicsSys_;
    std::unique_ptr<ParticleSystem> particleSys_;
    std::unique_ptr<NavigationSystem> navSys_;
    std::unique_ptr<RenderPipelineManager> renderPipelineMgr_;
    std::unique_ptr<AnimationSystem> animSys_;
    std::unique_ptr<GizmoSystem> gizmoSys_;
    std::unique_ptr<UndoRedoManager> undoMgr_;
    std::unique_ptr<PostProcessManager> postProc_;
    std::unique_ptr<EditorIntegration> editorInt_;
    std::unique_ptr<AudioSystem> audioSys_;
};

} // namespace BigHero::App