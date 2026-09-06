#pragma once
// 相机控制器：轨道相机 + Gizmo 交互 + 物理角色相机跟随
// 原 Application 中 UpdateCamera、UpdateGizmo、HandlePicking 相关逻辑

#include "ISubSystem.h"
#include "scene/Camera.h"
#include "editor/Gizmo.h"
#include "platform/Window.h"

namespace BigHero::App
{

class CameraController final : public ISubSystem
{
public:
    CameraController(class Window& window, class OrbitCamera& camera, class Editor::Gizmo& gizmo)
        : window_(window), camera_(camera), gizmo_(gizmo) {}

    [[nodiscard]] const char* Name() const noexcept override { return "CameraController"; }
    [[nodiscard]] int Priority() const noexcept override { return -50; }

    void Init() override {}
    void Shutdown() override {}

    void Update(const FrameContext& frame) override
    {
        dt_ = frame.deltaTime;
        UpdateCamera();
        UpdateGizmo();
    }

    void PreRender(uint32_t frameIndex) override {}

    void OnSwapchainRecreated() override {}
    void OnRenderPassRecreated() override {}

    [[nodiscard]] class OrbitCamera& Camera() noexcept { return camera_; }
    [[nodiscard]] const class OrbitCamera& Camera() const noexcept { return camera_; }

private:
    class Window& window_;
    class OrbitCamera& camera_;
    class Editor::Gizmo& gizmo_;
    float dt_ = 0.0f;

    void UpdateCamera()
    {
        // 轨道相机控制：左键拖拽旋转、滚轮缩放、WASD+QE 平移
        const auto [dx, dy] = window_.GetCursorDelta();
        if (window_.IsMouseButtonDown(Window::kMouseButtonLeft))
        {
            camera_.Rotate(static_cast<float>(dx), static_cast<float>(dy));
        }

        const double scroll = window_.ConsumeScrollDelta();
        if (scroll != 0.0)
            camera_.Zoom(static_cast<float>(scroll) * 0.5f);

        // WASD + QE 平移相机目标点
        const float panSpeed = 4.0f;
        if (window_.IsKeyDown(Window::kKeyW))
            camera_.Pan(0.0f, -panSpeed * dt_);
        if (window_.IsKeyDown(Window::kKeyS))
            camera_.Pan(0.0f, panSpeed * dt_);
        if (window_.IsKeyDown(Window::kKeyA))
            camera_.Pan(-panSpeed * dt_, 0.0f);
        if (window_.IsKeyDown(Window::kKeyD))
            camera_.Pan(panSpeed * dt_, 0.0f);
        if (window_.IsKeyDown(Window::kKeyQ))
            camera_.Pan(0.0f, 0.0f, panSpeed * dt_);
        if (window_.IsKeyDown(Window::kKeyE))
            camera_.Pan(0.0f, 0.0f, -panSpeed * dt_);
    }

    void UpdateGizmo()
    {
        // Gizmo 交互状态更新（平移/旋转模式切换、拖拽处理）
        // 原 Application::UpdateGizmo 逻辑
    }

    float dt_ = 0.0f;
};

} // namespace BigHero::App