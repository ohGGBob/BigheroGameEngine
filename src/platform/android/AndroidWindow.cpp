// Android 窗口后端实现：native_app_glue 事件泵 + 触摸→键鼠映射 + Android surface 创建。
#include "platform/android/AndroidWindow.h"

#include "core/Log.h"
#include "core/VkCheck.h"

#include <android/log.h>
#include <android/native_window.h>

#include <stdexcept>
#include <utility>

#define BH_ANDROID_LOG(...) __android_log_print(ANDROID_LOG_INFO, "BigHeroEngine", __VA_ARGS__)

namespace BigHero
{
AndroidWindow* AndroidWindow::s_active = nullptr;

namespace
{
struct android_app* g_androidAppSession = nullptr;
} // namespace

void SetAndroidAppSession(struct android_app* app) noexcept
{
    g_androidAppSession = app;
}

struct android_app* AndroidAppSession() noexcept
{
    return g_androidAppSession;
}

namespace
{
// AKEYCODE -> Window::kKey*（GLFW 编码值）映射；未列出的按键不映射
struct KeyMapEntry
{
    int32_t androidKey;
    int windowKey;
};
constexpr std::array<KeyMapEntry, 14> kKeyMap{{
    {AKEYCODE_A, Window::kKeyA},
    {AKEYCODE_D, Window::kKeyD},
    {AKEYCODE_E, Window::kKeyE},
    {AKEYCODE_P, Window::kKeyP},
    {AKEYCODE_Q, Window::kKeyQ},
    {AKEYCODE_S, Window::kKeyS},
    {AKEYCODE_W, Window::kKeyW},
    {AKEYCODE_Y, Window::kKeyY},
    {AKEYCODE_Z, Window::kKeyZ},
    {AKEYCODE_SPACE, Window::kKeySpace},
    {AKEYCODE_CTRL_LEFT, Window::kKeyLeftControl},
    {AKEYCODE_CTRL_RIGHT, Window::kKeyRightControl},
    {AKEYCODE_F5, Window::kKeyF5},
    {AKEYCODE_F9, Window::kKeyF9},
}};

int ToWindowKey(int32_t androidKey)
{
    for (const KeyMapEntry& e : kKeyMap)
        if (e.androidKey == androidKey)
            return e.windowKey;
    return -1;
}
} // namespace

AndroidWindow::AndroidWindow(struct android_app* app) : app_(app)
{
    s_active = this;
    app_->userData = this;
    BH_ANDROID_LOG("AndroidWindow created (native window %p)", reinterpret_cast<void*>(app_->window));
}

AndroidWindow::AndroidWindow()
{
    s_active = this; // headless 占位：无 native 窗口
}

AndroidWindow::~AndroidWindow()
{
    if (s_active == this)
        s_active = nullptr;
}

VkSurfaceKHR AndroidWindow::CreateSurface(VkInstance instance)
{
    if (app_ == nullptr || app_->window == nullptr)
        throw std::runtime_error("Android surface 创建失败：native 窗口未就绪");

    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = app_->window;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VK_CHECK(vkCreateAndroidSurfaceKHR(instance, &info, nullptr, &surface), "创建Android窗口表面");
    return surface;
}

bool AndroidWindow::ShouldClose() const
{
    if (terminated_.load())
        return true;
    return app_ != nullptr && app_->destroyRequested != 0;
}

void AndroidWindow::PollEvents() const
{
    PumpEvents();
}

void AndroidWindow::WaitEvents() const
{
    // 阻塞等待下一事件（含 -1 超时语义），处理完即返回
    int ident = 0, events = 0;
    android_poll_source* source = nullptr;
    ident = static_cast<int>(ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)));
    if (ident >= 0 && source != nullptr)
        source->process(app_, source);
}

void AndroidWindow::PumpEvents() const
{
    if (app_ == nullptr)
        return;

    // 排空全部待处理事件（命令 + 输入），与 NDK native-activity 样例一致
    int ident = 0, events = 0;
    android_poll_source* source = nullptr;
    while ((ident = static_cast<int>(ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source)))) >= 0)
    {
        if (source != nullptr)
            source->process(app_, source);
    }
}

std::pair<int, int> AndroidWindow::GetFramebufferSize() const
{
    if (app_ == nullptr || app_->window == nullptr)
        return {0, 0};
    return {ANativeWindow_getWidth(app_->window), ANativeWindow_getHeight(app_->window)};
}

bool AndroidWindow::IsMouseButtonDown(int button) const
{
    if (button == kMouseButtonLeft)
        return leftDown_;
    return false; // 触摸设备无右键
}

bool AndroidWindow::IsKeyDown(int key) const
{
    return key >= 0 && key < static_cast<int>(keyState_.size()) && keyState_[static_cast<size_t>(key)];
}

std::pair<double, double> AndroidWindow::GetCursorDelta()
{
    if (!cursorValid_)
    {
        cursorValid_ = true;
        lastCursorX_ = cursorX_;
        lastCursorY_ = cursorY_;
        return {0.0, 0.0};
    }
    const double dx = cursorX_ - lastCursorX_;
    const double dy = cursorY_ - lastCursorY_;
    lastCursorX_ = cursorX_;
    lastCursorY_ = cursorY_;
    return {dx, dy};
}

std::pair<double, double> AndroidWindow::GetCursorPos() const
{
    return {cursorX_, cursorY_};
}

bool AndroidWindow::ConsumeClick()
{
    // 语义与桌面一致：按下到释放位移小于阈值视为单击（用于拾取）
    if (!leftPressed_ && leftDown_)
    {
        leftPressed_ = true;
        pressX_ = cursorX_;
        pressY_ = cursorY_;
        return false;
    }
    if (leftPressed_ && !leftDown_)
    {
        leftPressed_ = false;
        const double dx = cursorX_ - pressX_;
        const double dy = cursorY_ - pressY_;
        return dx * dx + dy * dy < 25.0;
    }
    return false;
}

bool AndroidWindow::ConsumeRightClick()
{
    return false; // 触摸设备无右键（取消选择等交互后续可映射长按）
}

double AndroidWindow::ConsumeScrollDelta()
{
    const double delta = scrollDelta_;
    scrollDelta_ = 0.0;
    return delta;
}

bool AndroidWindow::ConsumeResizedFlag()
{
    const bool resized = resized_;
    resized_ = false;
    return resized;
}

void AndroidWindow::SetTitle(const std::string& title)
{
    (void)title; // Android 无窗口标题栏（FPS 等信息可经日志查看）
}

void AndroidWindow::HandleAppCmd(struct android_app* app, int32_t cmd)
{
    if (auto* self = static_cast<AndroidWindow*>(app->userData))
        self->OnAppCmd(cmd);
}

int32_t AndroidWindow::HandleInputEvent(struct android_app* app, AInputEvent* event)
{
    if (auto* self = static_cast<AndroidWindow*>(app->userData))
    {
        const int32_t type = AInputEvent_getType(event);
        if (type == AINPUT_EVENT_TYPE_MOTION)
        {
            self->OnMotionEvent(event);
            return 1;
        }
        if (type == AINPUT_EVENT_TYPE_KEY)
        {
            self->OnKeyEvent(event);
            return 1;
        }
    }
    return 0;
}

void AndroidWindow::OnAppCmd(int32_t cmd)
{
    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
        BH_ANDROID_LOG("APP_CMD_INIT_WINDOW");
        resized_ = true;
        break;
    case APP_CMD_TERM_WINDOW:
        // 单会话 v1：surface 被系统回收即结束主循环（恢复模式列入后续增强）
        BH_ANDROID_LOG("APP_CMD_TERM_WINDOW -> 结束会话");
        terminated_.store(true);
        break;
    case APP_CMD_DESTROY:
        BH_ANDROID_LOG("APP_CMD_DESTROY");
        terminated_.store(true);
        break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
    case APP_CMD_PAUSE:
    case APP_CMD_RESUME:
    case APP_CMD_START:
    case APP_CMD_STOP:
        // v1：不改变渲染循环（TERM/DESTROY 才结束）
        break;
    default:
        BH_ANDROID_LOG("APP_CMD %d", cmd);
        break;
    }
}

void AndroidWindow::OnMotionEvent(AInputEvent* event)
{
    const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    const size_t pointerCount = static_cast<size_t>(AMotionEvent_getPointerCount(event));
    const double x = AMotionEvent_getX(event, 0);
    const double y = AMotionEvent_getY(event, 0);

    switch (action)
    {
    case AMOTION_EVENT_ACTION_DOWN:
        cursorValid_ = false; // 新触摸序列：下一帧 GetCursorDelta 从零开始
        cursorX_ = x;
        cursorY_ = y;
        leftDown_ = true;
        break;
    case AMOTION_EVENT_ACTION_MOVE:
    {
        if (pointerCount >= 2)
        {
            // 双指：竖向滑动 -> 滚轮（屏幕 y 向下为正，滚轮向上为正）
            const double twoY = (AMotionEvent_getY(event, 0) + AMotionEvent_getY(event, 1)) * 0.5;
            if (lastTwoFingerY_ >= 0.0)
                scrollDelta_ += (lastTwoFingerY_ - twoY) * 0.1;
            lastTwoFingerY_ = twoY;
        }
        else
        {
            lastTwoFingerY_ = -1.0;
            cursorX_ = x;
            cursorY_ = y;
        }
        break;
    }
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
        leftDown_ = false;
        lastTwoFingerY_ = -1.0;
        break;
    default:
        break;
    }
}

void AndroidWindow::OnKeyEvent(AInputEvent* event)
{
    const int32_t action = AKeyEvent_getAction(event);
    const int key = ToWindowKey(AKeyEvent_getKeyCode(event));
    if (key < 0)
        return;

    if (action == AKEY_EVENT_ACTION_DOWN)
        keyState_[static_cast<size_t>(key)] = true;
    else if (action == AKEY_EVENT_ACTION_UP)
        keyState_[static_cast<size_t>(key)] = false;
}
} // namespace BigHero
