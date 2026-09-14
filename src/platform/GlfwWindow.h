#pragma once
// GLFW 窗口后端（桌面：Windows / Linux / macOS）。
// 实现 platform/Window.h 抽象接口；Vulkan surface 经 glfwCreateWindowSurface 创建。
// Android 平台不编译本文件（GLFW 不支持 Android，见 platform/android/AndroidWindow.cpp）。

#ifndef __ANDROID__

// 先引入 Window.h（含 vulkan.h）再包含 glfw3.h：GLFW_INCLUDE_NONE 下，
// 仅当 VK_VERSION_1_0 已定义时 GLFW 才声明 Vulkan surface 相关 API
#include "platform/Window.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace BigHero
{
// GLFW窗口RAII封装：窗口生命周期、帧缓冲尺寸变化标记、键鼠输入查询
class GlfwWindow final : public Window
{
  public:
    GlfwWindow(uint32_t width, uint32_t height, const char* title, bool visible = true);
    // Headless 模式：不创建窗口，仅初始化 GLFW（用于 CI headless 测试）
    explicit GlfwWindow(bool headless);

    ~GlfwWindow() override;

    [[nodiscard]] bool IsHeadless() const noexcept override { return headless_; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return window_; }
    [[nodiscard]] VkSurfaceKHR CreateSurface(VkInstance instance) override;

    [[nodiscard]] bool ShouldClose() const override;
    void PollEvents() const override;
    void WaitEvents() const override;

    [[nodiscard]] std::pair<int, int> GetFramebufferSize() const override;

    [[nodiscard]] bool IsMouseButtonDown(int button) const override;
    [[nodiscard]] bool IsKeyDown(int key) const override;

    [[nodiscard]] std::pair<double, double> GetCursorDelta() override;
    [[nodiscard]] std::pair<double, double> GetCursorPos() const override;

    [[nodiscard]] bool ConsumeClick() override;
    [[nodiscard]] bool ConsumeRightClick() override;
    [[nodiscard]] double ConsumeScrollDelta() override;
    [[nodiscard]] bool ConsumeResizedFlag() override;

    void SetTitle(const std::string& title) override;

    [[nodiscard]] GLFWwindow* Get() const noexcept { return window_; }

  private:
    static void ScrollCallback(GLFWwindow* window, double offsetX, double offsetY);
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    std::string title_;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    bool cursorValid_ = false;
    double scrollDelta_ = 0.0;
    bool framebufferResized_ = false;

    // 左键单击检测
    bool leftPressed_ = false;
    double pressX_ = 0.0;
    double pressY_ = 0.0;
    bool rightConsumed_ = false;

    bool headless_ = false;
};
} // namespace BigHero

#endif // !__ANDROID__
