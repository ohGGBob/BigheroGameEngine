// GLFW 窗口后端实现（桌面）。由 Window.cpp 工厂在非 Android 平台实例化。
#include "platform/GlfwWindow.h"

#ifndef __ANDROID__

#include "core/Log.h"
#include <stdexcept>

namespace BigHero
{
GlfwWindow::GlfwWindow(uint32_t width, uint32_t height, const char* title, bool visible)
    : headless_(false)
{
    if (glfwInit() != GLFW_TRUE)
        throw std::runtime_error("GLFW 初始化失败");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    window_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title, nullptr, nullptr);
    if (window_ == nullptr)
    {
        glfwTerminate();
        throw std::runtime_error("GLFW 创建窗口失败");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetScrollCallback(window_, ScrollCallback);
    glfwSetFramebufferSizeCallback(window_, FramebufferSizeCallback);
    LOG_INFO("窗口已创建: " << width << "x" << height << (visible ? "" : " (headless)"));
}

GlfwWindow::GlfwWindow(bool headless)
    : headless_(headless)
{
    if (glfwInit() != GLFW_TRUE)
        throw std::runtime_error("GLFW 初始化失败");

    // Headless: don't create a window
    LOG_INFO("Headless GLFW 初始化完成");
}

GlfwWindow::~GlfwWindow()
{
    if (!headless_ && window_ != nullptr)
        glfwDestroyWindow(window_);
    glfwTerminate();
}

VkSurfaceKHR GlfwWindow::CreateSurface(VkInstance instance)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, window_, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("创建窗口表面失败 (glfwCreateWindowSurface)");
    return surface;
}

bool GlfwWindow::ShouldClose() const
{
    if (headless_)
        return false;
    return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void GlfwWindow::PollEvents() const
{
    if (!headless_)
        glfwPollEvents();
}

void GlfwWindow::WaitEvents() const
{
    if (!headless_)
        glfwWaitEvents();
}

std::pair<int, int> GlfwWindow::GetFramebufferSize() const
{
    if (headless_)
        return {0, 0};
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {width, height};
}

bool GlfwWindow::IsMouseButtonDown(int button) const
{
    if (headless_)
        return false;
    return glfwGetMouseButton(window_, button) == GLFW_PRESS;
}

bool GlfwWindow::IsKeyDown(int key) const
{
    if (headless_)
        return false;
    return glfwGetKey(window_, key) == GLFW_PRESS;
}

std::pair<double, double> GlfwWindow::GetCursorDelta()
{
    if (headless_)
        return {0.0, 0.0};
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window_, &x, &y);

    if (!cursorValid_)
    {
        cursorValid_ = true;
        lastCursorX_ = x;
        lastCursorY_ = y;
        return {0.0, 0.0};
    }

    const double dx = x - lastCursorX_;
    const double dy = y - lastCursorY_;
    lastCursorX_ = x;
    lastCursorY_ = y;
    return {dx, dy};
}

std::pair<double, double> GlfwWindow::GetCursorPos() const
{
    if (headless_)
        return {0.0, 0.0};
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window_, &x, &y);
    return {x, y};
}

void GlfwWindow::SetTitle(const std::string& title)
{
    if (headless_)
        return;
    if (title_ == title)
        return;
    title_ = title;
    glfwSetWindowTitle(window_, title_.c_str());
}

bool GlfwWindow::ConsumeClick()
{
    if (headless_)
        return false;
    const int state = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT);
    const auto [x, y] = GetCursorPos();

    if (state == GLFW_PRESS && !leftPressed_)
    {
        leftPressed_ = true;
        pressX_ = x;
        pressY_ = y;
        return false;
    }
    if (state == GLFW_RELEASE && leftPressed_)
    {
        leftPressed_ = false;
        const double dx = x - pressX_;
        const double dy = y - pressY_;
        return dx * dx + dy * dy < 25.0;
    }
    return false;
}

bool GlfwWindow::ConsumeRightClick()
{
    if (headless_)
        return false;
    const bool pressed = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool consumed = pressed && !rightConsumed_;
    rightConsumed_ = pressed;
    return consumed;
}

double GlfwWindow::ConsumeScrollDelta()
{
    const double delta = scrollDelta_;
    scrollDelta_ = 0.0;
    return delta;
}

bool GlfwWindow::ConsumeResizedFlag()
{
    if (headless_)
        return false;
    const bool resized = framebufferResized_;
    framebufferResized_ = false;
    return resized;
}

void GlfwWindow::ScrollCallback(GLFWwindow* window, double offsetX, double offsetY)
{
    (void)offsetX;
    if (auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window)))
        self->scrollDelta_ += offsetY;
}

void GlfwWindow::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    (void)width;
    (void)height;
    if (auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window)))
        self->framebufferResized_ = true;
}
} // namespace BigHero

#endif // !__ANDROID__
