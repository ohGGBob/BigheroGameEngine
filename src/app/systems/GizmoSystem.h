#pragma once
// Gizmo 系统：三轴屏幕手柄 + 平移/旋转模式 + 编辑器交互
// 原 Application 中 gizmoMode_, gizmoDragAxis_, gizmoDragging_ 等逻辑

#include "ISubSystem.h"
#include "editor/Gizmo.h"
#include "platform/Window.h"
#include "scene/Camera.h"

namespace BigHero::App
{

class GizmoSystem final : public ISubSystem
{
public:
    GizmoSystem(class Window& window, Editor::Gizmo& gizmo)
        : window_(window), gizmo_(gizmo) {}

    [[nodiscard]] const char* Name() const noexcept override { return "GizmoSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return 10; }

    void Init() override {}

    void Shutdown() override {}

    void Update(const FrameContext& frame) override
    {
        UpdateGizmoInteraction();
    }

    void PreRender(uint32_t) override {}

    void OnSwapchainRecreated() override {}

    void OnRenderPassRecreated() override {}

    void SetMode(Editor::GizmoMode mode) { gizmo_.SetMode(mode); }
    [[nodiscard]] Editor::GizmoMode Mode() const noexcept { return gizmo_.Mode(); }

    [[nodiscard]] Editor::Gizmo& Gizmo() noexcept { return gizmo_; }
    [[nodiscard]] const Editor::Gizmo& Gizmo() const noexcept { return gizmo_; }

    void SetCamera(const OrbitCamera& camera) { camera_ = &camera; }
    void SetViewport(const VkExtent2D& extent) { viewport_ = extent; }

private:
    Window& window_;
    Editor::Gizmo& gizmo_;
    const OrbitCamera* camera_ = nullptr;
    VkExtent2D viewport_{800, 600};

    void UpdateGizmoInteraction()
    {
        // Gizmo 交互：鼠标左键拖拽轴、模式切换、点击拾取
    }
};

} // namespace BigHero::App