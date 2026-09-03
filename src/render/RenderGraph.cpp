#include "render/RenderGraph.h"
#include "core/VkCheck.h"
#include <string>

namespace BigHero::Render
{
VkImageLayout RenderGraph::UsageLayout(RGUsage usage) noexcept
{
    switch (usage)
    {
        case RGUsage::ColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RGUsage::DepthAttachment: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case RGUsage::DepthReadOnly: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case RGUsage::SampledRead: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RGUsage::PresentSrc: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkPipelineStageFlags RenderGraph::UsageStage(RGUsage usage) noexcept
{
    switch (usage)
    {
        case RGUsage::ColorAttachment: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case RGUsage::DepthAttachment:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case RGUsage::DepthReadOnly:
        case RGUsage::SampledRead: return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        case RGUsage::PresentSrc: return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}

VkAccessFlags RenderGraph::UsageWriteAccess(RGUsage usage) noexcept
{
    switch (usage)
    {
        case RGUsage::ColorAttachment: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case RGUsage::DepthAttachment: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case RGUsage::DepthReadOnly:
        case RGUsage::SampledRead:
        case RGUsage::PresentSrc: return 0;
    }
    return 0;
}

uint32_t RenderGraph::RegisterImage(const std::string& name, VkImage image, VkImageLayout initial)
{
    if (image == VK_NULL_HANDLE)
        return UINT32_MAX;
    const auto it = imageIndex_.find(image);
    if (it != imageIndex_.end())
        return it->second;

    RGImage rg{image, name, initial, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, false};
    images_.push_back(rg);
    const uint32_t idx = static_cast<uint32_t>(images_.size() - 1);
    imageIndex_.emplace(image, idx);
    return idx;
}

void RenderGraph::AddPass(const std::string& name, std::function<void()> record, std::vector<RGUsageDecl> usages)
{
    RGPass pass;
    pass.name = name;
    pass.record = std::move(record);
    pass.imageIndices.reserve(usages.size());
    pass.usages.reserve(usages.size());
    pass.endLayouts.reserve(usages.size());
    for (const auto& u : usages)
    {
        pass.imageIndices.push_back(RegisterImage("auto", u.image, VK_IMAGE_LAYOUT_UNDEFINED));
        pass.usages.push_back(u.usage);
        pass.endLayouts.push_back(u.endLayout != VK_IMAGE_LAYOUT_UNDEFINED ? u.endLayout : UsageLayout(u.usage));
    }
    passes_.push_back(std::move(pass));
}

void RenderGraph::Build()
{
    barriers_.clear();
    barrierPassIdx_.clear();
    for (auto& img : images_)
        img.writtenThisFrame = false;

    for (size_t p = 0; p < passes_.size(); ++p)
    {
        RGPass& pass = passes_[p];

        // 计算本 pass 每个资源进入时应处的布局；需要转换或存在跨 pass 读写依赖则记录 barrier
        for (size_t u = 0; u < pass.usages.size(); ++u)
        {
            const uint32_t idx = pass.imageIndices[u];
            if (idx == UINT32_MAX)
                continue;
            RGImage& img = images_[idx];
            const VkImageLayout target = UsageLayout(pass.usages[u]);
            const VkPipelineStageFlags dstStage = UsageStage(pass.usages[u]);
            const bool writes = UsageWriteAccess(pass.usages[u]) != 0;

            if (img.layout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                // 首次使用：直接转换到目标布局，忽略旧内容
                const VkPipelineStageFlags srcStage =
                    img.writtenThisFrame ? img.lastWriteStage : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                barriers_.push_back({img.image, VK_IMAGE_LAYOUT_UNDEFINED, target, srcStage, dstStage});
                barrierPassIdx_.push_back(static_cast<int32_t>(p));
                img.layout = target;
            }
            else if (img.layout != target)
            {
                // 布局不同：转换 + 同步（写后读 / 写后写）
                const VkPipelineStageFlags srcStage =
                    img.writtenThisFrame ? img.lastWriteStage : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                barriers_.push_back({img.image, img.layout, target, srcStage, dstStage});
                barrierPassIdx_.push_back(static_cast<int32_t>(p));
                img.layout = target;
            }
            else if (img.writtenThisFrame)
            {
                // 布局相同但本帧该资源已被先前 pass 写过：插入同布局内存 barrier（WAR/WAW 可见性）
                barriers_.push_back({img.image, img.layout, img.layout, img.lastWriteStage, dstStage});
                barrierPassIdx_.push_back(static_cast<int32_t>(p));
            }

            // 记录本 pass 的写访问（供下游同步）
            if (writes)
            {
                img.writtenThisFrame = true;
                img.lastWriteStage = dstStage;
            }

            // 本 pass 结束后资源布局 = endLayout（render pass finalLayout 由各 pass 自行保证）
            img.layout = pass.endLayouts[u];
        }
    }
}

void RenderGraph::Execute(VkCommandBuffer cmd) const
{
    if (cmd == VK_NULL_HANDLE)
        return;

    // 按 barrier 归属 pass 分组执行：barrier 在对应 pass 之前插入
    size_t barrierCursor = 0;
    const VkAccessFlags allAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    for (size_t p = 0; p < passes_.size(); ++p)
    {
        while (barrierCursor < barriers_.size() && barrierPassIdx_[barrierCursor] == static_cast<int32_t>(p))
        {
            const RGBarrierInfo& b = barriers_[barrierCursor];
            VkImageMemoryBarrier imb{};
            imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imb.oldLayout = b.oldLayout;
            imb.newLayout = b.newLayout;
            imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imb.image = b.image;
            imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (b.oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                b.oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
                b.newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                b.newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            {
                imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            imb.srcAccessMask = allAccess;
            imb.dstAccessMask = allAccess;
            vkCmdPipelineBarrier(cmd, b.srcStage, b.dstStage, 0, 0, nullptr, 0, nullptr, 1, &imb);
            ++barrierCursor;
        }
        passes_[p].record();
    }
}

VkImageLayout RenderGraph::ImageLayout(uint32_t imageIdx) const noexcept
{
    return imageIdx < images_.size() ? images_[imageIdx].layout : VK_IMAGE_LAYOUT_UNDEFINED;
}

void RenderGraph::Clear()
{
    images_.clear();
    passes_.clear();
    barriers_.clear();
    barrierPassIdx_.clear();
    imageIndex_.clear();
}
} // namespace BigHero::Render
