#include "render/RenderGraph.h"
#include "core/VkCheck.h"
#include <algorithm>
#include <string>

namespace BigHero::Render
{
VkImageLayout RenderGraph::UsageLayout(RGUsage usage) noexcept
{
    switch (usage)
    {
    case RGUsage::ColorAttachment:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case RGUsage::DepthAttachment:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case RGUsage::DepthReadOnly:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case RGUsage::SampledRead:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case RGUsage::PresentSrc:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkPipelineStageFlags RenderGraph::UsageStage(RGUsage usage) noexcept
{
    switch (usage)
    {
    case RGUsage::ColorAttachment:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case RGUsage::DepthAttachment:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case RGUsage::DepthReadOnly:
    case RGUsage::SampledRead:
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case RGUsage::DepthTestRead:
        // 深度测试发生在片元测试阶段（非片元着色器采样），同步阶段须覆盖之
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case RGUsage::PresentSrc:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
}

// 精确访问掩码：写角色=对应 WRITE bit，读角色=对应 READ bit，呈现无访问
VkAccessFlags RenderGraph::UsageAccess(RGUsage usage) noexcept
{
    switch (usage)
    {
    case RGUsage::ColorAttachment:
        return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case RGUsage::DepthAttachment:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case RGUsage::DepthReadOnly:
    case RGUsage::DepthTestRead:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    case RGUsage::SampledRead:
        return VK_ACCESS_SHADER_READ_BIT;
    case RGUsage::PresentSrc:
        return 0;
    }
    return 0;
}

uint32_t RenderGraph::RegisterImage(const std::string& name, VkImage image, VkImageLayout initial,
                                    VkDeviceSize sizeBytes, bool frameSharedMemory)
{
    if (image == VK_NULL_HANDLE)
        return UINT32_MAX;
    const auto it = imageIndex_.find(image);
    if (it != imageIndex_.end())
        return it->second;

    RGImage rg;
    rg.image = image;
    rg.name = name;
    rg.layout = initial;
    rg.sizeBytes = sizeBytes;
    rg.lastWriteStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    images_.push_back(rg);
    const uint32_t idx = static_cast<uint32_t>(images_.size() - 1);
    imageIndex_.emplace(image, idx);

    // 帧共享显存（transient 池同一槽位供各帧槽位实例复用）：登记为单成员别名组，
    // 首用屏障源取自身读写掩码并集，覆盖上一帧同槽位实例的残留访问
    if (frameSharedMemory)
    {
        images_[idx].aliasGroup = static_cast<int32_t>(aliasGroups_.size());
        aliasGroups_.push_back({idx});
    }
    return idx;
}

void RenderGraph::DeclareAlias(uint32_t imageA, uint32_t imageB)
{
    if (imageA >= images_.size() || imageB >= images_.size() || imageA == imageB)
        return;
    const int32_t ga = images_[imageA].aliasGroup;
    const int32_t gb = images_[imageB].aliasGroup;
    if (ga >= 0 && gb >= 0)
    {
        if (ga == gb)
            return;
        // 合并两组成一（组内成员的屏障源并集随之扩大）
        auto& dst = aliasGroups_[static_cast<size_t>(ga)];
        for (const uint32_t m : aliasGroups_[static_cast<size_t>(gb)])
        {
            images_[m].aliasGroup = ga;
            dst.push_back(m);
        }
        aliasGroups_[static_cast<size_t>(gb)].clear();
    }
    else if (ga >= 0)
    {
        aliasGroups_[static_cast<size_t>(ga)].push_back(imageB);
        images_[imageB].aliasGroup = ga;
    }
    else if (gb >= 0)
    {
        aliasGroups_[static_cast<size_t>(gb)].push_back(imageA);
        images_[imageA].aliasGroup = gb;
    }
    else
    {
        const int32_t g = static_cast<int32_t>(aliasGroups_.size());
        aliasGroups_.push_back({imageA, imageB});
        images_[imageA].aliasGroup = g;
        images_[imageB].aliasGroup = g;
    }
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
    {
        img.writtenThisFrame = false;
        img.lastWriteStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        img.lastWriteAccess = 0;
        img.firstUsePass = -1;
        img.lastUsePass = -1;
    }

    // ---- 预计算：资源生命周期 + 读/写阶段与访问掩码并集 ----
    // 帧结构逐帧一致：上一帧同资源（同槽位共享显存的实例）的访问阶段/掩码与本帧相同，
    // 故首用屏障源取"全部读写掩码并集"即可同时覆盖本帧组内前序使用与上帧残留访问。
    struct PreUse
    {
        int32_t first = -1;
        int32_t last = -1;
        VkPipelineStageFlags readStage = 0;
        VkPipelineStageFlags writeStage = 0;
        VkAccessFlags readAccess = 0;
        VkAccessFlags writeAccess = 0;
    };
    std::vector<PreUse> pre(images_.size());
    for (size_t p = 0; p < passes_.size(); ++p)
    {
        const RGPass& pass = passes_[p];
        for (size_t u = 0; u < pass.usages.size(); ++u)
        {
            const uint32_t idx = pass.imageIndices[u];
            if (idx == UINT32_MAX)
                continue;
            PreUse& pu = pre[idx];
            if (pu.first < 0)
                pu.first = static_cast<int32_t>(p);
            pu.last = static_cast<int32_t>(p);
            const VkPipelineStageFlags st = UsageStage(pass.usages[u]);
            const VkAccessFlags ac = UsageAccess(pass.usages[u]);
            if (ac & (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT))
            {
                pu.writeStage |= st;
                pu.writeAccess |= ac;
            }
            else
            {
                pu.readStage |= st;
                pu.readAccess |= ac;
            }
        }
    }

    // ---- 别名组屏障源：组内全部成员读写掩码并集（组=共享同一段显存的资源）----
    std::vector<VkPipelineStageFlags> aliasStage(images_.size(), 0);
    std::vector<VkAccessFlags> aliasAccess(images_.size(), 0);
    for (const auto& group : aliasGroups_)
    {
        VkPipelineStageFlags st = 0;
        VkAccessFlags ac = 0;
        for (const uint32_t m : group)
        {
            if (m >= images_.size() || pre[m].first < 0)
                continue;
            st |= pre[m].readStage | pre[m].writeStage;
            ac |= pre[m].readAccess | pre[m].writeAccess;
        }
        for (const uint32_t m : group)
        {
            if (m < images_.size())
            {
                aliasStage[m] = st;
                aliasAccess[m] = ac;
            }
        }
    }

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
            const bool firstUse = img.firstUsePass < 0;
            const VkImageLayout target = UsageLayout(pass.usages[u]);
            const VkPipelineStageFlags dstStage = UsageStage(pass.usages[u]);
            const VkAccessFlags dstAccess = UsageAccess(pass.usages[u]);

            // 生命周期记录（首次/最后一次使用）
            if (firstUse)
                img.firstUsePass = static_cast<int32_t>(p);
            img.lastUsePass = static_cast<int32_t>(p);

            // 别名/帧共享显存：首用屏障源改为组内读写掩码并集（含上一帧残留访问），
            // 显式覆盖"上一帧最后使用 → 本帧首次覆写"的 WAR/WAW 竞争
            const bool aliasFirst = firstUse && aliasStage[idx] != 0;
            const VkPipelineStageFlags firstSrcStage = aliasFirst ? aliasStage[idx] : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            const VkAccessFlags firstSrcAccess = aliasFirst ? aliasAccess[idx] : 0;

            if (img.layout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                // 首次使用：直接转换到目标布局，忽略旧内容；
                // 但别名/帧共享显存须保留组并集 srcAccess（覆盖上一帧残留写的 WAR/WAW 可见性）
                const VkPipelineStageFlags srcStage = img.writtenThisFrame ? img.lastWriteStage : firstSrcStage;
                const VkAccessFlags srcAccess = img.writtenThisFrame ? img.lastWriteAccess : firstSrcAccess;
                barriers_.push_back(
                    {img.image, VK_IMAGE_LAYOUT_UNDEFINED, target, srcStage, dstStage, srcAccess, dstAccess});
                barrierPassIdx_.push_back(static_cast<int32_t>(p));
                img.layout = target;
            }
            else if (img.layout != target)
            {
                // 布局不同：转换 + 同步（写后读 / 写后写），srcAccess 为上次写掩码
                const VkPipelineStageFlags srcStage = img.writtenThisFrame ? img.lastWriteStage : firstSrcStage;
                const VkAccessFlags srcAccess = img.writtenThisFrame ? img.lastWriteAccess : firstSrcAccess;
                barriers_.push_back({img.image, img.layout, target, srcStage, dstStage, srcAccess, dstAccess});
                barrierPassIdx_.push_back(static_cast<int32_t>(p));
                img.layout = target;
            }
            else if (img.writtenThisFrame)
            {
                // 布局相同但本帧该资源已被先前 pass 写过：插入同布局内存 barrier（WAR/WAW 可见性）
                barriers_.push_back(
                    {img.image, img.layout, img.layout, img.lastWriteStage, dstStage, img.lastWriteAccess, dstAccess});
                barrierPassIdx_.push_back(static_cast<int32_t>(p));
            }
            else if (aliasFirst)
            {
                // 别名组资源首次使用且布局已匹配（无需布局转换）：仍需插入同布局内存屏障，
                // 覆盖上一帧同槽位实例的残留访问（跨帧 WAR/WAW）
                barriers_.push_back(
                    {img.image, img.layout, img.layout, firstSrcStage, dstStage, firstSrcAccess, dstAccess});
                barrierPassIdx_.push_back(static_cast<int32_t>(p));
            }

            // 记录本 pass 的写访问（供下游同步）
            if (dstAccess & (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT))
            {
                img.writtenThisFrame = true;
                img.lastWriteStage = dstStage;
                img.lastWriteAccess = dstAccess;
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
            imb.srcAccessMask = b.srcAccess;
            imb.dstAccessMask = b.dstAccess;
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

RGLifetime RenderGraph::ResourceLifetime(uint32_t imageIdx) const noexcept
{
    if (imageIdx >= images_.size())
        return {};
    return {images_[imageIdx].firstUsePass, images_[imageIdx].lastUsePass};
}

std::vector<int32_t> RenderGraph::PlanTransientSlots() const
{
    std::vector<int32_t> slots(images_.size(), -1);

    // 收集已登记大小的活资源，按首次使用 pass 升序处理（贪心区间着色：区间不重叠 ⇒ 可共享槽位）
    struct Item
    {
        int32_t idx;
        int32_t first;
        int32_t last;
    };
    std::vector<Item> items;
    items.reserve(images_.size());
    for (size_t i = 0; i < images_.size(); ++i)
    {
        if (images_[i].sizeBytes == 0)
            continue;
        const RGLifetime life{images_[i].firstUsePass, images_[i].lastUsePass};
        if (!life.IsAlive())
            continue;
        items.push_back({static_cast<int32_t>(i), life.firstUse, life.lastUse});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.first < b.first; });

    std::vector<int32_t> slotLastUse; // 每个已开槽位的"最后使用 pass"（槽内资源不再重叠即可复用）
    for (const Item& it : items)
    {
        int32_t slot = -1;
        for (size_t s = 0; s < slotLastUse.size(); ++s)
        {
            if (slotLastUse[s] < it.first) // 该槽所有资源都先于本资源结束 → 可复用
            {
                slot = static_cast<int32_t>(s);
                break;
            }
        }
        if (slot < 0)
        {
            slot = static_cast<int32_t>(slotLastUse.size());
            slotLastUse.push_back(it.last);
        }
        else
        {
            slotLastUse[slot] = it.last;
        }
        slots[it.idx] = slot;
    }
    return slots;
}

void RenderGraph::Clear()
{
    images_.clear();
    passes_.clear();
    barriers_.clear();
    barrierPassIdx_.clear();
    imageIndex_.clear();
    aliasGroups_.clear();
}
} // namespace BigHero::Render
