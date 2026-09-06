#pragma once
// 编辑器集成：ImGui Overlay + 面板逻辑 + 回调桥接
// 原 Application 中 editorOverlay_, editorPanel_, SetupCallbacks 等逻辑

#include "ISubSystem.h"
#include "editor/EditorOverlay.h"
#include "editor/EditorPanel.h"
#include "render/Swapchain.h"

namespace BigHero::App
{

class EditorIntegration final : public ISubSystem
{
public:
    EditorIntegration() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "EditorIntegration"; }
    [[nodiscard]] int Priority() const noexcept override { return 40; }

    void Init(const Context& ctx, class Window& window, const Swapchain& swapchain)
    {
        editorOverlay_.Init(ctx, window, swapchain);
    }

    void Shutdown() override {}

    void Update(const FrameContext& frame) override
    {
        // 处理编辑器请求（保存/加载/增删物体/粒子爆发等）
        editorPanel_.ProcessRequests();
    }

    void PreRender(uint32_t frameIndex) override {}

    void OnSwapchainRecreated() override
    {
        editorOverlay_.RecreateFramebuffers(swapchain_);
    }

    void OnRenderPassRecreated() override {}

    void RecordUI(VkCommandBuffer cmd, uint32_t imageIndex, VkExtent2D extent)
    {
        editorOverlay_.Record(cmd, imageIndex, extent, [&](ImGuiContext* ctx)
        {
            editorPanel_.Draw(ctx);
        });
    }

    void OnSwapchainRecreated(const Swapchain& swapchain)
    {
        swapchain_ = &swapchain;
        editorOverlay_.RecreateFramebuffers(swapchain);
    }

    void OnRenderPassRecreated() override {}

    // 请求标志（由 EditorPanel 设置，由 Application 处理）
    bool saveRequested = false;
    bool loadRequested = false;
    bool addObjectRequested = false;
    bool deleteObjectRequested = false;

    [[nodiscard]] Editor::EditorOverlay& Overlay() noexcept { return editorOverlay_; }
    [[nodiscard]] const Editor::EditorOverlay& Overlay() const noexcept { return editorOverlay_; }
    [[nodiscard]] Editor::EditorPanel& Panel() noexcept { return editorPanel_; }
    [[nodiscard]] const Editor::EditorPanel& Panel() const noexcept { return editorPanel_; }

    [[nodiscard]] const char* Name() const noexcept override { return "EditorIntegration"; }
    [[nodiscard]] int Priority() const noexcept override { return 40; }

private:
    Editor::EditorOverlay editorOverlay_;
    Editor::EditorPanel editorPanel_;
    const Swapchain* swapchain_ = nullptr;
};

} // namespace BigHero::App