#pragma once
#include <cstdint>
#include <functional>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Window;

// Vulkan上下文：实例/校验层/窗口表面/物理与逻辑设备/队列，程序生命周期内持有
class Context
{
  public:
    explicit Context(Window& window, bool enableValidation = true);
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

    // 设备创建时按支持情况启用的采样器各向异性过滤
    [[nodiscard]] bool SamplerAnisotropyEnabled() const noexcept { return features_.samplerAnisotropy == VK_TRUE; }
    [[nodiscard]] float MaxSamplerAnisotropy() const noexcept { return properties_.limits.maxSamplerAnisotropy; }

    // 图形队列时间戳周期（纳秒/tick），GPU 性能剖析使用
    [[nodiscard]] float TimestampPeriod() const noexcept { return properties_.limits.timestampPeriod; }
    // 图形管线是否支持时间戳查询（Vulkan 1.2+ 核心特性，多数独显/集显均支持）
    [[nodiscard]] bool GraphicsTimestampSupported() const noexcept
    {
        return properties_.limits.timestampComputeAndGraphics == VK_TRUE;
    }

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
    VkPhysicalDeviceFeatures features_{};
    VkPhysicalDeviceProperties properties_{};
    VkCommandPool transferPool_ = VK_NULL_HANDLE;
};
} // namespace BigHero
