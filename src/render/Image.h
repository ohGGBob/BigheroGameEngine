#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;

// VkImage+显存+视图RAII封装，附带布局迁移与缓冲拷贝工具
class Image
{
  public:
    Image() = default;
    ~Image() { Destroy(); }

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Image(Image&& other) noexcept { MoveFrom(other); }
    Image& operator=(Image&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            MoveFrom(other);
        }
        return *this;
    }

    // 创建图像、分配显存并创建视图；布局保持UNDEFINED，由调用方按需迁移
    // arrayLayers>1 + CUBE_COMPATIBLE标志 + CUBE视图类型用于立方图
    void Create(const Context& ctx, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                VkMemoryPropertyFlags memProps, VkImageAspectFlags aspect, uint32_t mipLevels = 1,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, uint32_t arrayLayers = 1,
                VkImageCreateFlags flags = 0, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);
    void Destroy();

    // 通过一次性命令缓冲迁移图像布局
    void TransitionLayout(const Context& ctx, VkImageLayout oldLayout, VkImageLayout newLayout,
                          uint32_t layerCount = 1) const;

    // 通过一次性命令缓冲把整个Buffer拷入图像（要求图像已处于TRANSFER_DST_OPTIMAL布局）
    // layerCount>1时按层逐层拷贝（立方图上传）
    void CopyFromBuffer(const Context& ctx, VkBuffer buffer, uint32_t layerCount = 1) const;

    // GPU blit生成完整mip链：要求所有mip处于TRANSFER_DST布局（mip0已拷入），
    // 完成后全部mip迁移到SHADER_READ_ONLY。格式需支持线性过滤
    void GenerateMipmaps(const Context& ctx) const;

    [[nodiscard]] VkImage Get() const noexcept { return image_; }
    [[nodiscard]] VkImageView View() const noexcept { return view_; }
    [[nodiscard]] VkFormat Format() const noexcept { return format_; }
    [[nodiscard]] uint32_t Width() const noexcept { return width_; }
    [[nodiscard]] uint32_t Height() const noexcept { return height_; }
    [[nodiscard]] uint32_t MipLevels() const noexcept { return mipLevels_; }

    // 图像尺寸对应的标准mip级数（1x1时为1）
    [[nodiscard]] static uint32_t CalculateMipLevels(uint32_t width, uint32_t height)
    {
        uint32_t levels = 1;
        while (width > 1 || height > 1)
        {
            width = (width > 1) ? width / 2 : 1;
            height = (height > 1) ? height / 2 : 1;
            ++levels;
        }
        return levels;
    }

    // 检查格式是否支持线性过滤（mip blit缩放所需）
    [[nodiscard]] bool SupportsLinearFiltering(VkPhysicalDevice gpu) const;

  private:
    void MoveFrom(Image& other) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t mipLevels_ = 1;
};
} // namespace BigHero
