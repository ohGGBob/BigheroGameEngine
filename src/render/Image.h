#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>

#include "render/GpuAllocator.h"

namespace BigHero
{
class Context;
namespace Render
{
class MemoryPools;
}

// VkImage+显存+视图RAII封装，附带布局迁移与缓冲拷贝工具
// 显存来源（阶段2C收敛）：自管路径优先 MemoryPools 子分配（双池），池不可用时回退独占 vkAllocateMemory；
// CreateBound 外部显存路径不受影响
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

    // 创建图像但不绑定显存、也不立即创建视图（显存稍后经 BindExternalMemory 绑定到
    // transient 池共享槽位；供渲染图别名复用：先建全部组内图像 → 按内存需求分配槽位 → 统一绑定）
    // 视图参数被暂存（pending），在 BindExternalMemory 绑定成功后、于同一函数内创建视图，
    // 从而严格满足 VUID-01020（non-sparse image 建 view 前必须绑内存）。
    void CreateUnbound(const Context& ctx, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                       VkImageAspectFlags aspect, uint32_t mipLevels = 1,
                       VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, uint32_t arrayLayers = 1,
                       VkImageCreateFlags flags = 0, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);

    // 创建图像与视图，但显存由外部提供（transient 池子分配；memoryOffset 须已按内存需求对齐）。
    // 外部显存不归本对象所有：Destroy() 只销毁图像/视图，不释放显存。
    void CreateBound(const Context& ctx, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                     VkImageAspectFlags aspect, VkDeviceMemory externalMemory, VkDeviceSize memoryOffset,
                     uint32_t mipLevels = 1, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                     uint32_t arrayLayers = 1, VkImageCreateFlags flags = 0,
                     VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);

    // 把已创建（未绑定）的图像绑定到外部显存（transient 池共享槽位；Destroy 不释放外部显存）。
    // 若该图像由 CreateUnbound 创建（携带暂存的视图参数），绑定成功后在此创建视图，
    // 满足 VUID-01020；CreateBound 路径自行建视图，不设置 pending，不受影响。
    void BindExternalMemory(VkDeviceMemory memory, VkDeviceSize offset);

    // 若存在 CreateUnbound 暂存的视图参数且视图尚未创建，则创建视图。
    // 供不经 BindExternalMemory 直接绑定显存的路径（TransientAllocator 按原始 VkImage 句柄
    // 批量绑定）在绑定完成后显式调用，以确保视图晚于内存绑定创建（VUID-01020）。幂等。
    void FinalizePendingView();

    // 查询已创建图像的内存需求（transient 池对齐与大小计算用；不依赖当前绑定）
    [[nodiscard]] VkMemoryRequirements MemoryRequirements(const Context& ctx) const;
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
    // 创建 VkImage（不含显存分配/绑定/视图）：Create/CreateBound/CreateUnbound 共用前置
    void CreateImageOnly(const Context& ctx, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                         uint32_t mipLevels, VkSampleCountFlagBits samples, uint32_t arrayLayers,
                         VkImageCreateFlags flags);
    // 创建 VkImageView（要求 image_ 已绑定显存）：Create/CreateBound 在 bind 后调用；
    // CreateUnbound 不再直接调用，改为在 BindExternalMemory 绑定成功后由 pending 参数驱动调用
    void CreateView(VkImageAspectFlags aspect, uint32_t mipLevels, uint32_t arrayLayers, VkImageViewType viewType);

    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    bool externalMemory_ = false;          // true=显存由外部（transient 池）提供，析构不释放
    Render::GpuAllocation alloc_;          // 池子分配句柄（valid 时走池路径）
    Render::MemoryPools* pools_ = nullptr; // 池后端（alloc_.valid 时非空）
    VkImageView view_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t mipLevels_ = 1;

    // CreateUnbound 暂存的视图参数：待 BindExternalMemory 绑定成功后据此建视图（满足 01020）
    bool hasPendingView_ = false;
    VkImageAspectFlags pendingAspect_ = 0;
    uint32_t pendingMipLevels_ = 1;
    uint32_t pendingArrayLayers_ = 1;
    VkImageViewType pendingViewType_ = VK_IMAGE_VIEW_TYPE_2D;
};
} // namespace BigHero
