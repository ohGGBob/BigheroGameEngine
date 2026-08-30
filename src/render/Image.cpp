#include "render/Image.h"
#include "render/Context.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"

#include <stdexcept>

namespace BigHero
{
    void Image::Create(const Context& ctx, uint32_t width, uint32_t height, VkFormat format,
        VkImageUsageFlags usage, VkMemoryPropertyFlags memProps,
        VkImageAspectFlags aspect, uint32_t mipLevels, VkSampleCountFlagBits samples)
    {
        Destroy();

        device_ = ctx.Device();
        width_ = width;
        height_ = height;
        format_ = format;
        aspect_ = aspect;
        mipLevels_ = mipLevels;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = samples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateImage(device_, &imageInfo, nullptr, &image_), "创建Image");

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device_, image_, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(ctx.PhysicalDevice(), memReq.memoryTypeBits, memProps);
        if (allocInfo.memoryTypeIndex == UINT32_MAX)
            throw std::runtime_error("Image: 未找到满足属性的内存类型");

        VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, &memory_), "分配Image显存");
        VK_CHECK(vkBindImageMemory(device_, image_, memory_, 0), "绑定Image显存");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &view_), "创建Image视图");
    }

    void Image::Destroy()
    {
        if (device_ == VK_NULL_HANDLE)
            return;

        if (view_ != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, view_, nullptr);
            view_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_, image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
        device_ = VK_NULL_HANDLE;
        aspect_ = 0;
        width_ = 0;
        height_ = 0;
        mipLevels_ = 1;
    }

    void Image::TransitionLayout(const Context& ctx, VkImageLayout oldLayout, VkImageLayout newLayout) const
    {
        // 按用途推断布局迁移前后的访问掩码与阶段
        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        VkAccessFlags srcAccess = 0;
        VkAccessFlags dstAccess = VK_ACCESS_SHADER_READ_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess = 0;
            dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstAccess = VK_ACCESS_SHADER_READ_BIT;
        }
        else
        {
            throw std::runtime_error("Image::TransitionLayout: 不支持的布局组合");
        }

        ctx.SubmitOneTime([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image_;
            barrier.subresourceRange.aspectMask = aspect_;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevels_;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;

            vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });
    }

    void Image::CopyFromBuffer(const Context& ctx, VkBuffer buffer) const
    {
        ctx.SubmitOneTime([&](VkCommandBuffer cmd) {
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = aspect_;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { width_, height_, 1 };
            vkCmdCopyBufferToImage(cmd, buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        });
    }

    bool Image::SupportsLinearFiltering(VkPhysicalDevice gpu) const
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(gpu, format_, &props);
        return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    }

    void Image::GenerateMipmaps(const Context& ctx) const
    {
        if (mipLevels_ <= 1)
            return;
        if (!SupportsLinearFiltering(ctx.PhysicalDevice()))
            throw std::runtime_error("Image::GenerateMipmaps: 图像格式不支持线性过滤");

        ctx.SubmitOneTime([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = image_;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = aspect_;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            int32_t mipWidth = static_cast<int32_t>(width_);
            int32_t mipHeight = static_cast<int32_t>(height_);

            for (uint32_t level = 1; level < mipLevels_; ++level)
            {
                // 上一级mip转为传输源
                barrier.subresourceRange.baseMipLevel = level - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

                // blit缩小到当前级
                VkImageBlit blit{};
                blit.srcOffsets[0] = { 0, 0, 0 };
                blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
                blit.srcSubresource.aspectMask = aspect_;
                blit.srcSubresource.mipLevel = level - 1;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = { 0, 0, 0 };
                blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
                blit.dstSubresource.aspectMask = aspect_;
                blit.dstSubresource.mipLevel = level;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = 1;
                vkCmdBlitImage(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                // 当前级转为传输源，供下一级使用
                barrier.subresourceRange.baseMipLevel = level;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

                mipWidth = (mipWidth > 1) ? mipWidth / 2 : 1;
                mipHeight = (mipHeight > 1) ? mipHeight / 2 : 1;
            }

            // 全部mip迁移到着色器只读
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevels_;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        });
    }

    void Image::MoveFrom(Image& other) noexcept
    {
        device_ = other.device_;
        image_ = other.image_;
        memory_ = other.memory_;
        view_ = other.view_;
        format_ = other.format_;
        aspect_ = other.aspect_;
        width_ = other.width_;
        height_ = other.height_;
        mipLevels_ = other.mipLevels_;

        other.device_ = VK_NULL_HANDLE;
        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.view_ = VK_NULL_HANDLE;
        other.format_ = VK_FORMAT_UNDEFINED;
        other.aspect_ = 0;
        other.width_ = 0;
        other.height_ = 0;
        other.mipLevels_ = 1;
    }
}
