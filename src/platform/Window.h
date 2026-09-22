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
    static std::unique_ptr<Window> Create(uint32_t width, uint32_t height, const char* title, bool visible = true);
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

    // ---- 光标锁定（第一人称漫游）----
    // locked=true：隐藏并锁定光标于窗口内，鼠标位移全部用于转视角（桌面 GLFW 为
    // GLFW_CURSOR_DISABLED）。Android / headless 实现为空操作（触摸本就无需锁定）。
    virtual void SetCursorLocked(bool locked) = 0;
    [[nodiscard]] virtual bool IsCursorLocked() const noexcept = 0;

    // ---- 键位/按键常量（值 = GLFW 编码，桌面直通）----
    static constexpr int kMouseButtonLeft = 0;
    static constexpr int kMouseButtonRight = 1;
    static constexpr int kKeySpace = 32;
    static constexpr int kKeyTab = 258;       // GLFW_KEY_TAB（第一人称相机模式切换）
    static constexpr int kKeyLeftShift = 340; // GLFW_KEY_LEFT_SHIFT（FP 下降）
    static constexpr int kKeyA = 65;
    static constexpr int kKeyD = 68;
    static constexpr int kKeyE = 69;
    static constexpr int kKeyO = 79; // GLFW_KEY_O（自动昼夜循环）
    static constexpr int kKeyP = 80;
    static constexpr int kKeyQ = 81;
    static constexpr int kKeyS = 83;
    static constexpr int kKeyW = 87;
    static constexpr int kKeyY = 89;
    static constexpr int kKeyZ = 90;
    static constexpr int kKeyLeftControl = 341;
    static constexpr int kKeyRightControl = 345;
    static constexpr int kKeyLeftAlt = 342;
    static constexpr int kKeyRightShift = 344;
    // 功能键：GLFW 编码 F1=290 … F9=298 逐个递增。
    // 修正：原先 F5/F7/F8 写成 290/292/293，实际分别落在 F1/F3/F4 上，
    // 导致文档承诺的「F5 保存 / F7 资产库 / F8 LOD·探针·遮挡」快捷键全部错位。
    static constexpr int kKeyF1 = 290;
    static constexpr int kKeyF5 = 294;
    static constexpr int kKeyF7 = 296; // GLFW_KEY_F7（工程面板：资产数据库）
    static constexpr int kKeyF8 = 297; // GLFW_KEY_F8（工程面板：LOD/探针/遮挡）
    static constexpr int kKeyF9 = 298;
    // 漫游 / 展示厅按键（GLFW 编码，桌面直通）
    static constexpr int kKeyEscape = 256;
    static constexpr int kKeyEnter = 257;
    static constexpr int kKey1 = 49;
    static constexpr int kKey2 = 50;
    static constexpr int kKey3 = 51;
    static constexpr int kKey4 = 52;
    static constexpr int kKey5 = 53;
    static constexpr int kKey6 = 54;
    static constexpr int kKey7 = 55;
    static constexpr int kKey8 = 56;
    static constexpr int kKey9 = 57;
    static constexpr int kKeyB = 66;
    static constexpr int kKeyC = 67;
    static constexpr int kKeyF = 70;
    static constexpr int kKeyG = 71;
    static constexpr int kKeyH = 72;
    static constexpr int kKeyR = 82;
    static constexpr int kKeyT = 84;
    static constexpr int kKeyV = 86;
    static constexpr int kKeyX = 88;
    static constexpr int kKeyLeftBracket = 91;  // '['
    static constexpr int kKeyRightBracket = 93; // ']'

  protected:
    Window() = default;
};
} // namespace BigHero
