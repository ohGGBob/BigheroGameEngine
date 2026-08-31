#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdint>
#include <string>
#include <utility>

namespace BigHero
{
    // GLFW窗口RAII封装：窗口生命周期、帧缓冲尺寸变化标记、键鼠输入查询
    class Window
    {
    public:
        Window(uint32_t width, uint32_t height, const char* title);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        [[nodiscard]] GLFWwindow* Get() const noexcept { return window_; }

        [[nodiscard]] bool ShouldClose() const;
        void PollEvents() const;
        void WaitEvents() const;

        [[nodiscard]] std::pair<int, int> GetFramebufferSize() const;

        // ---- 输入查询 ----
        [[nodiscard]] bool IsMouseButtonDown(int button) const;
        [[nodiscard]] bool IsKeyDown(int key) const;

        // 自上次查询以来光标位移（内部维护上次光标位置）
        [[nodiscard]] std::pair<double, double> GetCursorDelta();

        // 窗口客户区光标坐标
        [[nodiscard]] std::pair<double, double> GetCursorPos() const;

        // 消费左键"单击"事件（按下到释放位移小于阈值，用于区分拖拽旋转）
        [[nodiscard]] bool ConsumeClick();

        // 消费右键按下状态（读取后归零，用于取消选择等）
        [[nodiscard]] bool ConsumeRightClick();

        // 消费本帧滚轮增量（y方向，向上为正），读取后归零
        [[nodiscard]] double ConsumeScrollDelta();

        // 消费帧缓冲尺寸变化标记，读取后归零
        [[nodiscard]] bool ConsumeResizedFlag();

        // 更新窗口标题（用于FPS显示等）
        void SetTitle(const std::string& title);

        static constexpr int kMouseButtonLeft = GLFW_MOUSE_BUTTON_LEFT;
        static constexpr int kKeyW = GLFW_KEY_W;
        static constexpr int kKeyA = GLFW_KEY_A;
        static constexpr int kKeyS = GLFW_KEY_S;
        static constexpr int kKeyD = GLFW_KEY_D;
        static constexpr int kKeyQ = GLFW_KEY_Q;
        static constexpr int kKeyE = GLFW_KEY_E;

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
    };
}
