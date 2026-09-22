#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Window;

namespace Render
{
class MemoryPools;
}

// Vulkan上下文：实例/校验层/窗口表面/物理与逻辑设备/队列，程序生命周期内持有
class Context
{
  public:
    explicit Context(Window& window, bool enableValidation = true);
    // Headless mode: no surface/swapchain (for CI validation)
    explicit Context(bool enableValidation = true);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    [[nodiscard]] VkInstance Instance() const noexcept { return instance_; }
    [[nodiscard]] VkSurfaceKHR Surface() const noexcept { return surface_; }
    [[nodiscard]] VkPhysicalDevice PhysicalDevice() const noexcept { return physicalDevice_; }
    [[nodiscard]] VkDevice Device() const noexcept { return device_; }
    [[nodiscard]] VkQueue GraphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] VkQueue PresentQueue() const noexcept { return presentQueue_; }
    [[nodiscard]] uint32_t GraphicsFamily() const noexcept { return graphicsFamily_; }
    [[nodiscard]] uint32_t PresentFamily() const noexcept { return presentFamily_; }
    [[nodiscard]] const char* PhysicalDeviceName() const noexcept { return properties_.deviceName; }
    [[nodiscard]] bool IsHeadless() const noexcept { return headless_; }

    // 设备创建时按支持情况启用的采样器各向异性过滤
    [[nodiscard]] bool SamplerAnisotropyEnabled() const noexcept { return features_.samplerAnisotropy == VK_TRUE; }
    [[nodiscard]] float MaxSamplerAnisotropy() const noexcept { return properties_.limits.maxSamplerAnisotropy; }

    // 双池显存子分配门面（Buffer/Image 通过它做子分配，避免逐资源 vkAllocateMemory）
    [[nodiscard]] Render::MemoryPools* Pools() const noexcept { return pools_.get(); }

    // 图形队列时间戳周期（纳秒/tick），GPU 性能剖析使用
    [[nodiscard]] float TimestampPeriod() const noexcept { return properties_.limits.timestampPeriod; }
    // 图形管线是否支持时间戳查询（Vulkan 1.2+ 核心特性，多数独显/集显均支持）
    [[nodiscard]] bool GraphicsTimestampSupported() const noexcept
    {
        return properties_.limits.timestampComputeAndGraphics == VK_TRUE;
    }

    // Physical device memory properties
    [[nodiscard]] const VkPhysicalDeviceMemoryProperties& PhysicalDeviceMemoryProperties() const noexcept
    {
        return memoryProperties_;
    }

    // Command pool
    [[nodiscard]] VkCommandPool CommandPool() const noexcept { return commandPool_; }

    // 在图形队列上提交一次性命令（临时命令缓冲），用于初始化期间的staging拷贝等
    void SubmitOneTime(const std::function<void(VkCommandBuffer)>& record) const;

    void WaitIdle() const { vkDeviceWaitIdle(device_); }

  private:
    void createInstance(bool enableValidation);
    void setupDebugMessenger();
    void createSurface(Window& window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT type,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                        void* userData);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsFamily_ = UINT32_MAX;
    uint32_t presentFamily_ = UINT32_MAX;
    // 跨队列接线现状（深化2）：B2(282cd24) 已让 RenderGraph 自动生成 image 的 queue family
    // ownership transfer barrier；Context 仅在 pickPhysicalDevice 探测并日志化"专用传输族"
    // （Render::SelectDedicatedTransferFamily）。当前所有命令池仍建在 graphicsFamily_，
    // 瞬态上传（FrameStaging/Buffer::UploadData）仍走图形队列 + SubmitOneTime。
    //
    // 为何本提交不把上传改投 transfer 队列：
    //   - 唯一有真实收益的是逐帧 FrameStaging 异步上传（与上一帧图形工作重叠），但需逐帧
    //     跨队列 semaphore + 缓冲所有权 release/acquire 乒乓，并与现有 inFlightFence 门控的
    //     bump 复位模型、FrameStaging 同队列 TRANSFER->VERTEX_INPUT barrier 交互，属多文件高
    //     同步风险改动；
    //   - 一次性同步上传（Buffer::UploadData，结尾 vkQueueWaitIdle）跨队列无重叠收益，纯换手开销；
    //   - 设备为 APU（共享内存/引擎），专用传输队列 n=1，实际重叠收益未测量，不宣称收益。
    // 故：探测+选型+回退已就绪，实际逐帧接线待有量化收益与更稳同步模型后再落地。
    VkPhysicalDeviceFeatures features_{};
    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceMemoryProperties memoryProperties_{};
    VkCommandPool transferPool_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    bool headless_ = false;
    // 显式管理生命期：~Context 函数体内（vkDestroyDevice 前）必须 pools_.reset()，
    // 否则 unique_ptr 成员析构晚于函数体，会在设备销毁后调用 vkFreeMemory
    std::unique_ptr<Render::MemoryPools> pools_;
};
} // namespace BigHero
