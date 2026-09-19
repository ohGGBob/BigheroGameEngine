#pragma once
// 平台窗口抽象层（ECS 之后的跨平台里程碑）：
// 桌面（Win/Linux/macOS）= GLFW 后端，Android = native_app_glue + ANativeWindow 后端。
// 上层（Application/Renderer/EditorOverlay/CameraController/GizmoSystem）只依赖本抽象接口，
// Vulkan surface 创建与实例扩展需求也经由此层，屏蔽 GLFW 与 Android NDK 的差异。
//
// 键位/鼠标按键常量沿用 GLFW 编码值（桌面端直通；Android 端按 AKEYCODE 映射，
// 无对应物理按键时查询返回 false）。触摸输入映射：单指=光标+左键，双指竖滑=滚轮。

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Window
{
  public:
    virtual ~Window() = default;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // ---- 平台工厂 ----
    // 桌面：GLFW 窗口；Android：native_app_glue 会话窗口（须在 INIT_WINDOW 之后构造）。
    static std::unique_ptr<Window> Create(uint32_t width, uint32_t height, const char* title,
                                          bool visible = true);
    // Headless 模式：不创建窗口（CI/离线校验），仅完成平台必要的初始化
    static std::unique_ptr<Window> CreateHeadless();

    // Vulkan 实例扩展需求（平台相关）：桌面=GLFW 提供，Android=VK_KHR_surface+VK_KHR_android_surface。
    // 静态函数：headless Context 不持有窗口时也需要同一扩展集合。
    [[nodiscard]] static std::vector<const char*> RequiredSurfaceInstanceExtensions();

    [[nodiscard]] virtual bool IsHeadless() const noexcept = 0;

    // 原生窗口句柄：桌面=GLFWwindow*，Android=ANativeWindow*（供 ImGui 平台后端等使用）
    [[nodiscard]] virtual void* NativeWindowHandle() const noexcept = 0;

    // 创建 Vulkan 窗口表面（失败抛异常）
    [[nodiscard]] virtual VkSurfaceKHR CreateSurface(VkInstance instance) = 0;

    // ---- 窗口/输入查询（与原 GLFW 版 Window 语义一致）----
    [[nodiscard]] virtual bool ShouldClose() const = 0;
    virtual void PollEvents() const = 0;
    virtual void WaitEvents() const = 0;

    [[nodiscard]] virtual std::pair<int, int> GetFramebufferSize() const = 0;

    [[nodiscard]] virtual bool IsMouseButtonDown(int button) const = 0;
    [[nodiscard]] virtual bool IsKeyDown(int key) const = 0;

    // 自上次查询以来光标位移（内部维护上次光标位置）
    [[nodiscard]] virtual std::pair<double, double> GetCursorDelta() = 0;

    // 窗口客户区光标坐标
    [[nodiscard]] virtual std::pair<double, double> GetCursorPos() const = 0;

    // 消费左键"单击"事件（按下到释放位移小于阈值，用于区分拖拽旋转）
    [[nodiscard]] virtual bool ConsumeClick() = 0;

    // 消费右键按下状态（读取后归零，用于取消选择等）
    [[nodiscard]] virtual bool ConsumeRightClick() = 0;

    // 消费本帧滚轮增量（y方向，向上为正），读取后归零
    [[nodiscard]] virtual double ConsumeScrollDelta() = 0;

    // 消费帧缓冲尺寸变化标记，读取后归零
    [[nodiscard]] virtual bool ConsumeResizedFlag() = 0;

    // 更新窗口标题（用于FPS显示等；Android 无标题栏，实现为空操作）
    virtual void SetTitle(const std::string& title) = 0;

    // ---- 键位/按键常量（值 = GLFW 编码，桌面直通）----
    static constexpr int kMouseButtonLeft = 0;
    static constexpr int kMouseButtonRight = 1;
    static constexpr int kKeySpace = 32;
    static constexpr int kKeyTab = 258;     // GLFW_KEY_TAB（第一人称相机模式切换）
    static constexpr int kKeyLeftShift = 340; // GLFW_KEY_LEFT_SHIFT（FP 下降）
    static constexpr int kKeyA = 65;
    static constexpr int kKeyD = 68;
    static constexpr int kKeyE = 69;
    static constexpr int kKeyP = 80;
    static constexpr int kKeyQ = 81;
    static constexpr int kKeyS = 83;
    static constexpr int kKeyW = 87;
    static constexpr int kKeyY = 89;
    static constexpr int kKeyZ = 90;
    static constexpr int kKeyLeftControl = 341;
    static constexpr int kKeyRightControl = 345;
    static constexpr int kKeyF5 = 290;
    static constexpr int kKeyF9 = 298;

  protected:
    Window() = default;
};
} // namespace BigHero
