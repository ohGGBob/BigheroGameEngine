// 平台窗口工厂 + 跨平台公共实现。
// 桌面 -> GlfwWindow；Android -> AndroidWindow（platform/android/AndroidWindow.cpp）。
#include "platform/Window.h"

#if defined(__ANDROID__)
#include "platform/android/AndroidWindow.h"
#else
#include "platform/GlfwWindow.h"
#endif

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace BigHero
{
std::unique_ptr<Window> Window::Create(uint32_t width, uint32_t height, const char* title, bool visible)
{
#if defined(__ANDROID__)
    (void)visible;
    (void)title;
    return std::make_unique<AndroidWindow>(AndroidAppSession());
#else
    return std::make_unique<GlfwWindow>(width, height, title, visible);
#endif
}

std::unique_ptr<Window> Window::CreateHeadless()
{
#if defined(__ANDROID__)
    return std::make_unique<AndroidWindow>(); // Android headless：会话窗口未就绪的占位实现
#else
    return std::make_unique<GlfwWindow>(true); // headless：仅初始化GLFW，不创建窗口
#endif
}

std::vector<const char*> Window::RequiredSurfaceInstanceExtensions()
{
#if defined(__ANDROID__)
    // NativeActivity 路径：无 GLFW，直接声明 Android surface 扩展
    static const char* kAndroidExts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    return {std::begin(kAndroidExts), std::end(kAndroidExts)};
#else
    uint32_t count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&count);
    if (exts == nullptr)
        throw std::runtime_error("GLFW 未提供 Vulkan 实例扩展（Vulkan 不可用?）");
    return {exts, exts + count};
#endif
}
} // namespace BigHero
