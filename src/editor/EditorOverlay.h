#pragma once
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
class Window;
class Swapchain;

// Dear ImGui编辑器覆盖层：
// - 独立的UI渲染通道（loadOp=LOAD，在场景渲染之上绘制，并完成到PRESENT布局的转换）
// - 初始化ImGui核心与GLFW/Vulkan后端（含中文字体加载）
// - 每帧 NewFrame() -> 构建面板 -> Render(cmd) 录制到命令缓冲
class EditorOverlay
{
  public:
    EditorOverlay() = default;
    ~EditorOverlay() { Shutdown(); }

    EditorOverlay(const EditorOverlay&) = delete;
    EditorOverlay& operator=(const EditorOverlay&) = delete;

    void Init(const Context& ctx, const Window& window, const Swapchain& swapchain);

    // 交换链重建后调用：重建UI帧缓冲并同步图像数
    void RecreateFramebuffers(const Swapchain& swapchain);
    void Shutdown();

    // ---- 每帧三段式 ----
    void NewFrame();

    // 在命令缓冲内录制UI渲染通道与ImGui绘制数据（须处于场景渲染通道之外）
    void Render(VkCommandBuffer cmd, uint32_t imageIndex);

  private:
    void createFramebuffers(const Swapchain& swapchain);
    void destroyFramebuffers();

    const Context* ctx_ = nullptr;
    VkRenderPass overlayPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    bool initialized_ = false;
};
} // namespace BigHero
