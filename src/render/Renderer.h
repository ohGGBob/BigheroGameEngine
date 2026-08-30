#pragma once
#include "render/Image.h"
#include "render/Swapchain.h"
#include "render/render_pass.h"
#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
#include <cstdint>

namespace BigHero
{
    class Context;
    class Window;

    // 帧渲染器：渲染通道/帧缓冲/深度附件/命令缓冲/同步对象，
    // 负责每帧"等待-采集-录制-提交-呈现"循环以及窗口尺寸变化时的交换链重建
    class Renderer
    {
    public:
        Renderer(const Context& ctx, Window& window);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        // 每帧调用：处理尺寸变化与最小化等待，随后录制并提交一帧
        // prePass(cmd, frameIndex, extent)（可选）：主渲染通道之前的深度预通道（阴影贴图等）
        // recordScene(cmd, frameIndex, extent)：由外部负责绑定管线、描述符与几何体并下达绘制命令
        // frameIndex用于选取该帧并行槽位独立的UBO/描述符
        // recordUi(cmd, imageIndex, extent)（可选）：场景通道结束后在UI覆盖层通道中录制界面
        void DrawFrame(const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordScene,
            const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordUi = {},
            const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& prePass = {});

        // 交换链重建完成后回调（供覆盖层等依赖交换链图像的资源重建）
        void SetResizeCallback(std::function<void()> callback) { resizeCallback_ = std::move(callback); }

        [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return renderPass_.renderPass; }
        [[nodiscard]] const Swapchain& GetSwapchain() const noexcept { return swapchain_; }
        [[nodiscard]] VkExtent2D Extent() const noexcept { return swapchain_.Extent(); }
        // 当前MSAA采样数（渲染通道与图形管线需保持一致）
        [[nodiscard]] VkSampleCountFlagBits SampleCount() const noexcept { return sampleCount_; }
        [[nodiscard]] static constexpr uint32_t MaxFramesInFlight() noexcept { return kMaxFrames; }

    private:
        static constexpr uint32_t kMaxFrames = 2;

        [[nodiscard]] VkFormat pickDepthFormat() const;
        [[nodiscard]] VkSampleCountFlagBits pickSampleCount() const;
        void createFrameResources();
        void destroyFrameResources();
        void createCommandResources();
        void createSyncObjects();
        void destroySyncObjects();
        void handleResize();

        const Context& ctx_;
        Window& window_;

        Swapchain swapchain_;
        Render::RenderPass renderPass_;
        VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
        VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_1_BIT;

        // MSAA中间图像：整个交换链共用一套，随交换链重建
        Image msaaColorImage_;
        Image msaaDepthImage_;

        std::vector<VkFramebuffer> framebuffers_;

        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers_;

        // 同步对象：imageAvailable与栅栏按帧并行数、renderFinished按交换链图像数
        std::vector<VkSemaphore> imageAvailableSemaphores_;
        std::vector<VkSemaphore> renderFinishedSemaphores_;
        std::vector<VkFence> inFlightFences_;
        uint32_t currentFrame_ = 0;

        std::function<void()> resizeCallback_;
    };
}
