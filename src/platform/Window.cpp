#include "platform/Window.h"
#include "core/Log.h"
#include <stdexcept>

namespace BigHero
{
Window::Window(uint32_t width, uint32_t height, const char* title, bool visible)
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

Window::~Window()
{
    if (window_ != nullptr)
        glfwDestroyWindow(window_);
    glfwTerminate();
}

bool Window::ShouldClose() const
{
    return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void Window::PollEvents() const
{
    glfwPollEvents();
}

void Window::WaitEvents() const
{
    glfwWaitEvents();
}

std::pair<int, int> Window::GetFramebufferSize() const
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {width, height};
}

bool Window::IsMouseButtonDown(int button) const
{
    return glfwGetMouseButton(window_, button) == GLFW_PRESS;
}

bool Window::IsKeyDown(int key) const
{
    return glfwGetKey(window_, key) == GLFW_PRESS;
}

void Window::SetTitle(const std::string& title)
{
    if (title_ == title)
        return;
    title_ = title;
    glfwSetWindowTitle(window_, title_.c_str());
}

std::pair<double, double> Window::GetCursorDelta()
{
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

std::pair<double, double> Window::GetCursorPos() const
{
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window_, &x, &y);
    return {x, y};
}

bool Window::ConsumeClick()
{
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
        // 位移小于5像素视为单击（拖拽旋转不触发拾取）
        return dx * dx + dy * dy < 25.0;
    }
    return false;
}

bool Window::ConsumeRightClick()
{
    const bool pressed = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool consumed = pressed && !rightConsumed_;
    rightConsumed_ = pressed;
    return consumed;
}

double Window::ConsumeScrollDelta()
{
    const double delta = scrollDelta_;
    scrollDelta_ = 0.0;
    return delta;
}

bool Window::ConsumeResizedFlag()
{
    const bool resized = framebufferResized_;
    framebufferResized_ = false;
    return resized;
}

void Window::ScrollCallback(GLFWwindow* window, double offsetX, double offsetY)
{
    (void)offsetX;
    if (auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window)))
        self->scrollDelta_ += offsetY;
}

void Window::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    (void)width;
    (void)height;
    if (auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window)))
        self->framebufferResized_ = true;
}
} // namespace BigHero

