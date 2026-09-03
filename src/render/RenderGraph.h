#pragma once
// 帧渲染图（Render Graph）：声明式 Pass 调度 + 自动布局转换/跨 Pass 同步。
//
// 设计目标（对标商业引擎渲染图核心，聚焦当前管线实际需求）：
//   - 把 DrawFrame 的硬编码 pass 链改为"声明式 pass 图"：每个 pass 声明它读写哪些
//     图像资源以及以何种角色（颜色/深度附件、采样、呈现）访问。
//   - 渲染图据此推导每个资源在 pass 间的布局，自动插入 vkCmdPipelineBarrier，
//     替代手写 barrier（如 PostProcessor 里的深度布局转换）与人工维护的布局链。
//   - 新 pass 只需 RegisterImage + AddPass 即可安全接入，无需关心下游布局状态。
//
// 与现有 render pass 的关系：渲染图不接管每个 pass 内部 render pass 的 attachment
// 转换（那些由 render pass 定义 initialLayout/finalLayout 自行完成），只负责"跨 pass"
// 的布局转换与同步。每个 pass 的录制回调在渲染图准备好的布局下执行。
// pass 声明 usages 时可用 endLayout 告知"本 pass 结束后资源所处的布局"
// （即该 pass render pass 的 finalLayout），渲染图据此维护资源布局状态。
//
// 纯逻辑 + Vulkan 命令录制两层：布局推导/barrier 生成可离线单测（不依赖窗口）。

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
// 资源在图中的使用角色（决定目标布局与访问阶段）
enum class RGUsage : uint8_t
{
    ColorAttachment, // 作为颜色附件写入：COLOR_ATTACHMENT_OPTIMAL，写阶段 COLOR_ATTACHMENT_OUTPUT
    DepthAttachment, // 作为深度附件写入：DEPTH_STENCIL_ATTACHMENT_OPTIMAL，写阶段 EARLY|LATE_FRAGMENT_TESTS
    DepthReadOnly,   // 采样深度（只读）：DEPTH_STENCIL_READ_ONLY_OPTIMAL，读阶段 FRAGMENT_SHADER
    SampledRead,     // 采样颜色/纹理：SHADER_READ_ONLY_OPTIMAL，读阶段 FRAGMENT_SHADER
    PresentSrc       // 呈现源：PRESENT_SRC_KHR（仅交换链输出，无访问阶段）
};

// 单个 pass 对某个资源的访问声明
struct RGUsageDecl
{
    VkImage image = VK_NULL_HANDLE; // 图资源（须已 RegisterImage）
    RGUsage usage = RGUsage::SampledRead;
    // 本 pass 结束后资源所处布局（即该资源在本 pass render pass 中的 finalLayout）。
    // 0 = 保持与 usage 目标布局一致。深度附件写后采样读、颜色附件写后采样读等场景需显式给出。
    VkImageLayout endLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// 一条待插入的布局转换 barrier（供单测断言与调试）
struct RGBarrierInfo
{
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags srcStage = 0;
    VkPipelineStageFlags dstStage = 0;
};

class RenderGraph
{
  public:
    // 注册一个图像为图资源，initial 为其当前布局（UNDEFINED 表示内容不关心/首次使用）。
    // 同一 VkImage 重复注册返回既有索引。返回稳定索引（0 起）。
    uint32_t RegisterImage(const std::string& name, VkImage image,
                           VkImageLayout initial = VK_IMAGE_LAYOUT_UNDEFINED);

    // 添加一个 pass：record 在渲染图把全部 usages 布局就绪后调用（无参：命令缓冲由
    // Execute 传入的 cmd 经闭包捕获，录制逻辑关注资源准备与绘制命令本身）。
    // usages 声明本 pass 对资源的访问角色；未声明的资源保持原布局。
    void AddPass(const std::string& name, std::function<void()> record, std::vector<RGUsageDecl> usages = {});

    // 构建：按添加顺序生成 barrier 序列（幂等：重复调用先清空规划）。
    void Build();

    // 执行：在 cmd 上按序录制 barrier + pass 录制回调。需先 Build()。
    void Execute(VkCommandBuffer cmd) const;

    // ---- 只读查询（供单测/调试） ----
    [[nodiscard]] uint32_t PassCount() const noexcept { return static_cast<uint32_t>(passes_.size()); }
    [[nodiscard]] uint32_t ImageCount() const noexcept { return static_cast<uint32_t>(images_.size()); }
    // 构建后待插入的 barrier 序列
    [[nodiscard]] const std::vector<RGBarrierInfo>& PlannedBarriers() const noexcept { return barriers_; }
    // 每条 barrier 之前的 pass 下标（-1=无，0=第一个 pass 前）
    [[nodiscard]] const std::vector<int32_t>& BarrierPassIndex() const noexcept { return barrierPassIdx_; }
    // 查询某资源当前（规划后）布局
    [[nodiscard]] VkImageLayout ImageLayout(uint32_t imageIdx) const noexcept;

    // 清空全部状态（pass 与资源），供重建
    void Clear();

  private:
    struct RGImage
    {
        VkImage image = VK_NULL_HANDLE;
        std::string name;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; // 当前已知布局
        VkPipelineStageFlags lastWriteStage = 0;          // 最近一次写访问阶段
        bool writtenThisFrame = false;                    // 本帧内是否已被某个 pass 写过
    };
    struct RGPass
    {
        std::string name;
        std::function<void()> record;
        std::vector<uint32_t> imageIndices;      // usages 对应的资源下标（等长）
        std::vector<RGUsage> usages;             // 每个资源在本 pass 的角色
        std::vector<VkImageLayout> endLayouts;   // 每个资源在本 pass 结束后的布局
    };

    [[nodiscard]] static VkImageLayout UsageLayout(RGUsage usage) noexcept;
    [[nodiscard]] static VkPipelineStageFlags UsageStage(RGUsage usage) noexcept;
    [[nodiscard]] static VkAccessFlags UsageWriteAccess(RGUsage usage) noexcept;

    std::vector<RGImage> images_;
    std::vector<RGPass> passes_;
    std::vector<RGBarrierInfo> barriers_; // Build 生成的 barrier 序列
    std::vector<int32_t> barrierPassIdx_; // 每条 barrier 之前的 pass 下标
    std::unordered_map<VkImage, uint32_t> imageIndex_;
};
} // namespace BigHero::Render
