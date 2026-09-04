#include "render/Swapchain.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "platform/Window.h"
#include "render/Context.h"

#include <algorithm>
#include <limits>

namespace BigHero
{
namespace
{
VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    }
    return formats[0];
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes)
{
    for (VkPresentModeKHR mode : modes)
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            return mode;
    return VK_PRESENT_MODE_FIFO_KHR; // 三种模式保证支持
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, Window& window)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    const auto [w, h] = window.GetFramebufferSize();
    VkExtent2D actual{static_cast<uint32_t>(std::max(w, 1)), static_cast<uint32_t>(std::max(h, 1))};
    actual.width = std::clamp(actual.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    actual.height = std::clamp(actual.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return actual;
}
} // namespace

void Swapchain::Create(const Context& ctx, Window& window, VkSwapchainKHR oldHandle)
{
    VkPhysicalDevice gpu = ctx.PhysicalDevice();
    VkSurfaceKHR surface = ctx.Surface();

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, formats.data());

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, modes.data());

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = choosePresentMode(modes);
    const VkExtent2D extent = chooseSwapExtent(caps, window);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // 图形与呈现不在同一队列族时使用并发模式，省去手动所有权转移
    const uint32_t queueFamilies[] = {ctx.GraphicsFamily(), ctx.PresentFamily()};
    if (ctx.GraphicsFamily() != ctx.PresentFamily())
    {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = queueFamilies;
    }
    else
    {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = oldHandle;

    device_ = ctx.Device();
    VK_CHECK(vkCreateSwapchainKHR(device_, &info, nullptr, &handle_), "创建交换链");
    format_ = surfaceFormat.format;
    extent_ = extent;

    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(device_, handle_, &actualImageCount, nullptr);
    images_.resize(actualImageCount);
    vkGetSwapchainImagesKHR(device_, handle_, &actualImageCount, images_.data());

    views_.resize(actualImageCount);
    for (uint32_t i = 0; i < actualImageCount; ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &views_[i]), "创建交换链图像视图");
    }

    LOG_INFO("交换链创建成功，图像数: " << actualImageCount << "，尺寸: " << extent.width << "x" << extent.height
                                        << "，格式: " << format_);
}

void Swapchain::Destroy()
{
    if (device_ == VK_NULL_HANDLE)
        return;

    for (VkImageView view : views_)
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device_, view, nullptr);
    views_.clear();
    images_.clear();

    if (handle_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

void Swapchain::MoveFrom(Swapchain& other) noexcept
{
    device_ = other.device_;
    handle_ = other.handle_;
    format_ = other.format_;
    extent_ = other.extent_;
    images_ = std::move(other.images_);
    views_ = std::move(other.views_);

    other.device_ = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
    other.format_ = VK_FORMAT_UNDEFINED;
    other.extent_ = {};
}
} // namespace BigHero

