#include "render/MemoryPools.h"

#include <algorithm>
#include <string>

#include "core/Log.h"
#include "core/VkCheck.h"

namespace BigHero::Render
{
namespace
{
constexpr VkDeviceSize kDeviceBlockSize = 256ull * 1024 * 1024; // 256MB × 8 = 2GB 上限
constexpr VkDeviceSize kHostBlockSize = 64ull * 1024 * 1024;    // 64MB × 8 = 512MB 上限
constexpr VkDeviceSize kMinAlignFloor = 256;                    // 与 bufferImageGranularity 取大
} // namespace

MemoryPools::MemoryPools(VkPhysicalDevice gpu, VkDevice device) : device_(device)
{
    vkGetPhysicalDeviceMemoryProperties(gpu, &memProps_);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gpu, &props);
    // minAlign：GpuBlockAllocator 同时取整分配大小与偏移，保证 buffer/image 可安全同块共存
    const VkDeviceSize minAlign = std::max<VkDeviceSize>(props.limits.bufferImageGranularity, kMinAlignFloor);

    // deviceLocal：首选纯显存（避免占用 host 可见窗口/BAR）
    deviceTypeIdx_ = FindMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    // hostVisible：首选 HOST_VISIBLE|HOST_COHERENT 且非 DEVICE_LOCAL（优先系统内存而非 BAR）
    hostTypeIdx_ = FindMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (deviceTypeIdx_ != UINT32_MAX)
    {
        devicePool_ = std::make_unique<GpuAllocator>(kDeviceBlockSize, minAlign, kMaxBlocks,
                                                     [this](uint32_t /*blockIndex*/, VkDeviceSize size)
                                                     { return AllocateRaw(deviceTypeIdx_, size); });
    }
    if (hostTypeIdx_ != UINT32_MAX)
    {
        hostPool_ = std::make_unique<GpuAllocator>(kHostBlockSize, minAlign, kMaxBlocks,
                                                   [this](uint32_t blockIndex, VkDeviceSize size)
                                                   {
                                                       VkDeviceMemory mem = AllocateRaw(hostTypeIdx_, size);
                                                       if (mem != VK_NULL_HANDLE)
                                                       {
                                                           // 块级持久映射：建块时映射一次，子分配直接指针写入
                                                           void* mapped = nullptr;
                                                           VK_CHECK(vkMapMemory(device_, mem, 0, size, 0, &mapped),
                                                                    "MemoryPools 块持久映射");
                                                           hostMapped_[blockIndex] = mapped;
                                                       }
                                                       return mem;
                                                   });
    }

    LOG_INFO("显存池初始化: deviceLocal "
             << (devicePool_ ? "type " + std::to_string(deviceTypeIdx_) + "（256MB×8）" : "不可用") << ", hostVisible "
             << (hostPool_ ? "type " + std::to_string(hostTypeIdx_) + "（64MB×8，持久映射）" : "不可用"));
}

MemoryPools::~MemoryPools()
{
    LogStats();
    if (device_ == VK_NULL_HANDLE)
        return;

    // 归还所有块显存（子分配应在持有方析构前全部 Free）
    if (devicePool_)
    {
        for (uint32_t b = 0; b < devicePool_->BlockCount(); ++b)
            if (VkDeviceMemory mem = devicePool_->MemoryOf(b))
                vkFreeMemory(device_, mem, nullptr);
    }
    if (hostPool_)
    {
        for (uint32_t b = 0; b < hostPool_->BlockCount(); ++b)
            if (VkDeviceMemory mem = hostPool_->MemoryOf(b))
                vkFreeMemory(device_, mem, nullptr);
    }
    hostMapped_.clear();
}

GpuAllocation MemoryPools::Alloc(VkMemoryPropertyFlags requiredProps, const VkMemoryRequirements& memReq)
{
    GpuAllocation result{};
    if (memReq.size == 0)
        return result;

    // 路由：含 HOST_VISIBLE 走 host 池，否则 device 池；池类型必须满足全部要求属性
    GpuAllocator* pool = nullptr;
    uint32_t typeIdx = UINT32_MAX;
    if ((requiredProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
    {
        if (hostPool_ && TypeSatisfies(hostTypeIdx_, requiredProps))
        {
            pool = hostPool_.get();
            typeIdx = hostTypeIdx_;
        }
    }
    else if (devicePool_ && TypeSatisfies(deviceTypeIdx_, requiredProps))
    {
        pool = devicePool_.get();
        typeIdx = deviceTypeIdx_;
    }

    if (pool == nullptr || memReq.size > pool->BlockSize() || (memReq.memoryTypeBits & (1u << typeIdx)) == 0)
    {
        // 超出块大小 / 内存类型不含池类型 / 池不可用 → 调用方回退独占 vkAllocateMemory
        ++fallbacks_;
        return result;
    }

    result = pool->Alloc(memReq.size, memReq.alignment);
    if (result.valid && (requiredProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
        result.block |= kHostPoolBit;
    return result;
}

void MemoryPools::Free(const GpuAllocation& a)
{
    if (!a.valid)
        return;

    GpuAllocation sub = a;
    sub.block = DecodeBlock(a);
    if (IsHostAlloc(a))
    {
        if (hostPool_)
            hostPool_->Free(sub);
    }
    else if (devicePool_)
    {
        devicePool_->Free(sub);
    }
}

VkDeviceMemory MemoryPools::MemoryOf(const GpuAllocation& a) const
{
    if (!a.valid)
        return VK_NULL_HANDLE;

    const uint32_t blockIdx = DecodeBlock(a);
    if (IsHostAlloc(a))
        return hostPool_ ? hostPool_->MemoryOf(blockIdx) : VK_NULL_HANDLE;
    return devicePool_ ? devicePool_->MemoryOf(blockIdx) : VK_NULL_HANDLE;
}

void* MemoryPools::MappedOf(const GpuAllocation& a) const
{
    if (!a.valid || !IsHostAlloc(a))
        return nullptr;

    const auto it = hostMapped_.find(DecodeBlock(a));
    if (it == hostMapped_.end() || it->second == nullptr)
        return nullptr;
    return static_cast<char*>(it->second) + a.offset;
}

void MemoryPools::LogStats() const
{
    auto usedOf = [](const GpuAllocator& pool)
    {
        VkDeviceSize total = 0;
        for (uint32_t b = 0; b < pool.BlockCount(); ++b)
            total += pool.Used(b);
        return total;
    };

    if (devicePool_)
        LOG_INFO("[MemoryPools] deviceLocal: "
                 << devicePool_->BlockCount() << "/" << devicePool_->MaxBlocks() << " 块, 已用 "
                 << usedOf(*devicePool_) / (1024ull * 1024) << "MB / "
                 << devicePool_->BlockCount() * devicePool_->BlockSize() / (1024ull * 1024) << "MB");
    if (hostPool_)
        LOG_INFO("[MemoryPools] hostVisible: " << hostPool_->BlockCount() << "/" << hostPool_->MaxBlocks()
                                               << " 块, 已用 " << usedOf(*hostPool_) / (1024ull * 1024) << "MB / "
                                               << hostPool_->BlockCount() * hostPool_->BlockSize() / (1024ull * 1024)
                                               << "MB");
    LOG_INFO("[MemoryPools] 独占分配回退次数: " << fallbacks_);
}

VkDeviceMemory MemoryPools::AllocateRaw(uint32_t typeIndex, VkDeviceSize size)
{
    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = size;
    info.memoryTypeIndex = typeIndex;

    VkDeviceMemory mem = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(device_, &info, nullptr, &mem), "MemoryPools 块分配");
    return mem;
}

uint32_t MemoryPools::FindMemoryType(VkMemoryPropertyFlags required, VkMemoryPropertyFlags avoid) const
{
    // 第一轮：满足 required 且不含 avoid
    for (uint32_t t = 0; t < memProps_.memoryTypeCount; ++t)
    {
        const VkMemoryPropertyFlags flags = memProps_.memoryTypes[t].propertyFlags;
        if ((flags & required) == required && (flags & avoid) == 0)
            return t;
    }
    if (avoid == 0)
        return UINT32_MAX;
    // 第二轮退让：仅满足 required（如 iGPU 统一内存同时带 DEVICE_LOCAL|HOST_VISIBLE）
    for (uint32_t t = 0; t < memProps_.memoryTypeCount; ++t)
    {
        const VkMemoryPropertyFlags flags = memProps_.memoryTypes[t].propertyFlags;
        if ((flags & required) == required)
            return t;
    }
    return UINT32_MAX;
}

bool MemoryPools::TypeSatisfies(uint32_t typeIndex, VkMemoryPropertyFlags requiredProps) const
{
    return typeIndex < memProps_.memoryTypeCount &&
           (memProps_.memoryTypes[typeIndex].propertyFlags & requiredProps) == requiredProps;
}
} // namespace BigHero::Render
