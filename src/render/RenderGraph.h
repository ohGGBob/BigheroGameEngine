#pragma once
// 帧渲染图（Render Graph）：声明式 Pass 调度 + 自动布局转换/跨 Pass 精确同步 + 资源生命周期管理。
//
// 设计目标（对标商业引擎渲染图核心，如 Frostbite FrameGraph 的资源管理与同步自动化）：
//   - 把 DrawFrame 的硬编码 pass 链改为"声明式 pass 图"：每个 pass 声明它读写哪些
//     图像资源以及以何种角色（颜色/深度附件、采样、呈现）访问。
//   - 渲染图据此推导每个资源在 pass 间的布局，自动插入 vkCmdPipelineBarrier，
//     替代手写 barrier（如 PostProcessor 里的深度布局转换）与人工维护的布局链。
//   - 精确同步：每条 barrier 的 src/dst 阶段与访问掩码由 usage 角色推导（而非保守全掩码），
//     减少 GPU pipeline stall。
//   - 资源生命周期：Build 时计算每个资源"首次使用 pass → 最后使用 pass"区间，
//     供 transient 内存池做别名复用（生命周期不重叠的资源共享显存）与内存报告。
//
// 与现有 render pass 的关系：渲染图不接管每个 pass 内部 render pass 的 attachment
// 转换（那些由 render pass 定义 initialLayout/finalLayout 自行完成），只负责"跨 pass"
// 的布局转换与同步。每个 pass 的录制回调在渲染图准备好的布局下执行。
// pass 声明 usages 时可用 endLayout 告知"本 pass 结束后资源所处的布局"
// （即该 pass render pass 的 finalLayout），渲染图据此维护资源布局状态。
//
// 纯逻辑 + Vulkan 命令录制两层：布局推导/barrier 生成/生命周期分析可离线单测（不依赖窗口）。

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
// 资源在图中的使用角色（决定目标布局与访问阶段/掩码）
enum class RGUsage : uint8_t
{
    ColorAttachment, // 颜色附件写入：COLOR_ATTACHMENT_OPTIMAL，写阶段 COLOR_ATTACHMENT_OUTPUT
    DepthAttachment, // 深度附件写入：DEPTH_STENCIL_ATTACHMENT_OPTIMAL，写阶段 EARLY|LATE_FRAGMENT_TESTS
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
    // UNDEFINED = 保持与 usage 目标布局一致。深度附件写后采样读、颜色附件写后采样读等场景需显式给出。
    VkImageLayout endLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// 一条待插入的布局转换 barrier（含精确阶段/访问掩码，供单测断言与调试）
struct RGBarrierInfo
{
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags srcStage = 0;
    VkPipelineStageFlags dstStage = 0;
    VkAccessFlags srcAccess = 0;
    VkAccessFlags dstAccess = 0;
};

// 资源生命周期区间（pass 下标，含端点）：[firstUse, lastUse]，均有效时表示资源活过这些 pass
struct RGLifetime
{
    int32_t firstUse = -1; // 首次被使用的 pass 下标（-1=未使用）
    int32_t lastUse = -1;  // 最后被使用的 pass 下标（-1=未使用）
    [[nodiscard]] bool IsAlive() const noexcept { return firstUse >= 0 && lastUse >= 0; }
    // 两个生命周期是否在 pass 顺序上重叠（重叠 ⇒ 不可共享显存/不可相互覆盖）
    [[nodiscard]] bool Overlaps(const RGLifetime& o) const noexcept
    {
        return IsAlive() && o.IsAlive() && firstUse <= o.lastUse && o.firstUse <= lastUse;
    }
};

class RenderGraph
{
  public:
    // 注册一个图像为图资源，initial 为其当前布局（UNDEFINED 表示内容不关心/首次使用）。
    // sizeBytes 为其显存需求（transient 池分配/内存报告用，0=未知）。
    // 同一 VkImage 重复注册返回既有索引。返回稳定索引（0 起）。
    uint32_t RegisterImage(const std::string& name, VkImage image, VkImageLayout initial = VK_IMAGE_LAYOUT_UNDEFINED,
                           VkDeviceSize sizeBytes = 0);

    // 添加一个 pass：record 在渲染图把全部 usages 布局就绪后调用（无参：命令缓冲由
    // Execute 传入的 cmd 经闭包捕获，录制逻辑关注资源准备与绘制命令本身）。
    // usages 声明本 pass 对资源的访问角色；未声明的资源保持原布局。
    void AddPass(const std::string& name, std::function<void()> record, std::vector<RGUsageDecl> usages = {});

    // 构建：按添加顺序生成 barrier 序列与资源生命周期（幂等：重复调用先清空规划）。
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
    // 查询某资源生命周期区间（需先 Build）
    [[nodiscard]] RGLifetime ResourceLifetime(uint32_t imageIdx) const noexcept;
    [[nodiscard]] const std::string& ImageName(uint32_t imageIdx) const noexcept
    {
        static const std::string kEmpty;
        return imageIdx < images_.size() ? images_[imageIdx].name : kEmpty;
    }
    [[nodiscard]] VkDeviceSize ImageSizeBytes(uint32_t imageIdx) const noexcept
    {
        return imageIdx < images_.size() ? images_[imageIdx].sizeBytes : 0;
    }

    // 计算 transient 内存槽位：对已登记 sizeBytes 的资源按生命周期区间做区间着色（interval
    // coloring 贪心），把区间互不重叠的资源分到同一槽位（槽位=可共享的一段显存）。
    // 返回每个 imageIdx 的槽位索引；-1=未登记大小或未使用。需先 Build()。纯逻辑，供内存报告与池布局。
    [[nodiscard]] std::vector<int32_t> PlanTransientSlots() const;

    // 清空全部状态（pass 与资源），供重建
    void Clear();

  private:
    struct RGImage
    {
        VkImage image = VK_NULL_HANDLE;
        std::string name;
        VkDeviceSize sizeBytes = 0;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; // 当前已知布局
        VkPipelineStageFlags lastWriteStage = 0;          // 最近一次写访问阶段
        VkAccessFlags lastWriteAccess = 0;                // 最近一次写访问掩码
        bool writtenThisFrame = false;                    // 本帧内是否已被某个 pass 写过
        int32_t firstUsePass = -1;                        // 生命周期区间（Build 填充）
        int32_t lastUsePass = -1;
    };
    struct RGPass
    {
        std::string name;
        std::function<void()> record;
        std::vector<uint32_t> imageIndices;    // usages 对应的资源下标（等长）
        std::vector<RGUsage> usages;           // 每个资源在本 pass 的角色
        std::vector<VkImageLayout> endLayouts; // 每个资源在本 pass 结束后的布局
    };

    [[nodiscard]] static VkImageLayout UsageLayout(RGUsage usage) noexcept;
    [[nodiscard]] static VkPipelineStageFlags UsageStage(RGUsage usage) noexcept;
    [[nodiscard]] static VkAccessFlags UsageAccess(RGUsage usage) noexcept;

    std::vector<RGImage> images_;
    std::vector<RGPass> passes_;
    std::vector<RGBarrierInfo> barriers_; // Build 生成的 barrier 序列
    std::vector<int32_t> barrierPassIdx_; // 每条 barrier 之前的 pass 下标
    std::unordered_map<VkImage, uint32_t> imageIndex_;
};
} // namespace BigHero::Render
