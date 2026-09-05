#pragma once
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Window;
class Context;

// 交换链RAII封装：句柄/图像/视图/格式/尺寸；窗口尺寸变化时通过重建完成更新
class Swapchain
{
  public:
    Swapchain() = default;
    ~Swapchain() { Destroy(); }

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    Swapchain(Swapchain&& other) noexcept { MoveFrom(other); }
    Swapchain& operator=(Swapchain&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            MoveFrom(other);
        }
        return *this;
    }

    // 创建交换链；可传入旧交换链句柄以复用其图像，加速重建
    void Create(const Context& ctx, Window& window, VkSwapchainKHR oldHandle = VK_NULL_HANDLE);
    void Destroy();

    [[nodiscard]] VkSwapchainKHR Handle() const noexcept { return handle_; }
    [[nodiscard]] VkFormat Format() const noexcept { return format_; }
    [[nodiscard]] VkExtent2D Extent() const noexcept { return extent_; }
    [[nodiscard]] const std::vector<VkImage>& Images() const noexcept { return images_; }
    [[nodiscard]] const std::vector<VkImageView>& Views() const noexcept { return views_; }
    [[nodiscard]] uint32_t ImageCount() const noexcept { return static_cast<uint32_t>(images_.size()); }
    [[nodiscard]] bool IsValid() const noexcept { return handle_ != VK_NULL_HANDLE; }

  private:
    void MoveFrom(Swapchain& other) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR handle_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
};
} // namespace BigHero
