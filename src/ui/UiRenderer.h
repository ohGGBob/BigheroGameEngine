#pragma once
// 运行时 UI 批渲染器（GPU 侧）：全部矩形/文本四边形展开为顶点进单一大顶点缓冲，
// 单 draw call（图集单一绑定，无切换）；alpha 混合、无深度、渲染于场景之后编辑器 ImGui 之前。
//
// 渲染通道约定（与编辑器覆盖层 EditorOverlay 配合）：本通道 loadOp=LOAD 画在交换链图像上，
// finalLayout 保持 COLOR_ATTACHMENT_OPTIMAL，随后 ImGui 覆盖层通道 LOAD 并转为 PRESENT。
// 帧缓冲随交换链重建（OnSwapchainRecreated）；图集描述符由 UiFontAtlas 持有、经
// SetAtlasLayout 注册进管线布局。
//
// 顶点缓冲按帧在飞槽位（kMaxFrames=2）各一份 host-visible 映射缓冲，Record 前由调用方
// UploadVertices 直写本帧槽位，规避 CPU 写与 GPU 读的跨帧竞争。

#include "render/Buffer.h"
#include "render/Image.h"
#include "render/pipeline.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
class Swapchain;

namespace Ui
{
// UI 顶点（56 字节；与 shaders/ui.vert.glsl 输入布局一一对应）
struct UiVertex
{
    glm::vec2 pos;   // 屏幕像素（左上原点，y 向下）
    glm::vec2 local; // 相对矩形中心的偏移（像素；圆角 SDF 用，文本/直角填 0）
    glm::vec4 color; // 顶点色 RGBA 0-1
    glm::vec2 uv;    // 图集归一化 UV（纯色 = 白像素 UV）
    glm::vec4 rect;  // halfW, halfH, cornerRadius(px), 保留 0
};
static_assert(sizeof(UiVertex) == 56, "UiVertex 须为 56 字节（与着色器属性布局一致）");

class UiRenderer
{
  public:
    static constexpr uint32_t kMaxFrames = 2;
    static constexpr VkDeviceSize kInitialVertexBytes = 512 * 1024; // 起步容量（约 9400 四边形）
    static constexpr uint32_t kVertexStride = sizeof(UiVertex);

    UiRenderer() = default;
    ~UiRenderer() { Destroy(); }
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    // 创建渲染通道 + 逐交换链图像帧缓冲 + 双槽位顶点缓冲（host-visible 映射）
    void Init(const Context& ctx, const Swapchain& swapchain);
    // 管线（依赖 Init 的渲染通道与外部图集描述符布局/集合；着色器 shaders/ui.vert.spv / ui.frag.spv）。
    // 集合句柄稳定：图集扩容只重写集合内容（vkUpdateDescriptorSets），无须重新注册
    void CreatePipeline(VkDevice dev, VkDescriptorSetLayout atlasLayout, VkDescriptorSet atlasSet);
    // 交换链重建：重建帧缓冲（渲染通道/管线与格式绑定，格式变化场景与覆盖层通道同等对待——
    // 现有引擎在格式变化时仅重建主管线族，本通道与覆盖层一致沿用）
    void OnSwapchainRecreated(const Swapchain& swapchain);
    void Destroy();

    // 直写 frameSlot 槽位顶点缓冲（容量不足时扩容重建该槽位缓冲）
    void UploadVertices(const Context& ctx, uint32_t frameSlot, const UiVertex* data, size_t count);
    // 录制 UI 渲染通道：begin(LOAD) → 绑管线/描述符/视口 → 顶点槽位绘制 → end
    void Record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frameSlot, uint32_t vertexCount, VkExtent2D extent);

    [[nodiscard]] bool IsValid() const noexcept
    {
        return ctx_ != nullptr && renderPass_ != VK_NULL_HANDLE && pipeline_.has_value() && pipeline_->IsValid();
    }
    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return renderPass_; }

  private:
    void CreateRenderPass(VkFormat format);
    void CreateFramebuffers(const Swapchain& swapchain);
    void DestroyFramebuffers();
    void EnsureVertexCapacity(const Context& ctx, uint32_t frameSlot, VkDeviceSize bytes);
    [[nodiscard]] const Buffer& VertexBuffer(uint32_t frameSlot) const;

    const Context* ctx_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    std::optional<Render::GraphicsPipeline> pipeline_;
    Render::GraphicsPipelineConfig config_;
    std::array<Buffer, kMaxFrames> vertexBuffers_;
    VkDescriptorSetLayout atlasLayout_ = VK_NULL_HANDLE; // UiFontAtlas::Layout()
    VkDescriptorSet atlasSet_ = VK_NULL_HANDLE;          // UiFontAtlas::Set()（句柄稳定）
};
} // namespace Ui
} // namespace BigHero
