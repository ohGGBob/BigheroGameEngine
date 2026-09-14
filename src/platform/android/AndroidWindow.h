#pragma once
// Android 窗口后端：native_app_glue 会话窗口 + 触摸输入 → 抽象 Window 接口映射。
// 仅在 Android 平台编译（CMake ANDROID 分支）。
//
// 输入映射约定（v1 单会话）：
//   - 单指触摸 = 光标移动 + 左键按下/抬起（支撑拖拽旋转、Gizmo、UI 点击）
//   - 双指竖向滑动 = 滚轮增量（缩放）
//   - 外接键盘（部分设备/模拟器）按 AKEYCODE 映射 WASD/QE/CTRL/Z/Y/P/Space/F5/F9

#ifndef __ANDROID__
#error "AndroidWindow 仅限 Android 平台编译"
#endif

#include "platform/Window.h"

#include <android/input.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <array>
#include <atomic>

namespace BigHero
{
// native_app_glue 会话句柄：AndroidMain 启动时注册，Window::Create 工厂在
// Android 平台经此获取 android_app*（Application 层无感知，不传平台参数）。
void SetAndroidAppSession(struct android_app* app) noexcept;
[[nodiscard]] struct android_app* AndroidAppSession() noexcept;

class AndroidWindow final : public Window
{
  public:
    // 正常会话窗口：须在 APP_CMD_INIT_WINDOW 之后构造（app->window 非 null）
    explicit AndroidWindow(struct android_app* app);
    // Headless 占位：不关联 native 窗口（Android 上仅用于 validate-only 路径）
    AndroidWindow();

    ~AndroidWindow() override;

    [[nodiscard]] bool IsHeadless() const noexcept override { return app_ == nullptr; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return app_ ? app_->window : nullptr; }
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

    // native_app_glue 回调（由 AndroidMain 注册，转发到当前窗口实例）
    static void HandleAppCmd(struct android_app* app, int32_t cmd);
    static int32_t HandleInputEvent(struct android_app* app, AInputEvent* event);

  private:
    void PumpEvents() const;
    void OnAppCmd(int32_t cmd);
    void OnMotionEvent(AInputEvent* event);
    void OnKeyEvent(AInputEvent* event);

    struct android_app* app_ = nullptr;

    // 输入状态（PumpEvents 在 const 上下文中更新，使用 mutable/原子）
    mutable double cursorX_ = 0.0;
    mutable double cursorY_ = 0.0;
    mutable double lastCursorX_ = 0.0;
    mutable double lastCursorY_ = 0.0;
    mutable bool cursorValid_ = false;
    mutable bool leftDown_ = false;
    mutable double scrollDelta_ = 0.0;
    mutable bool resized_ = false;

    bool leftPressed_ = false; // ConsumeClick 用：按下-释放位移检测
    double pressX_ = 0.0;
    double pressY_ = 0.0;

    // 触摸按住即视为左键按下；Android 无右键，ConsumeRightClick 恒 false
    mutable std::array<bool, 512> keyState_{}; // 下标 = Window::kKey* 常量值（GLFW 编码，最大 345）
    std::atomic<bool> terminated_{false};      // 单会话：TERM_WINDOW/DESTROY 后结束主循环

    mutable double lastTwoFingerY_ = -1.0; // 双指滚轮映射的参考 y（<0 表示无效）

    static AndroidWindow* s_active; // glue 回调 → 实例转发
};
} // namespace BigHero
