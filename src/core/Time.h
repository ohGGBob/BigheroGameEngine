#pragma once
// 跨平台时间源：单一稳定时钟，替代直接调用 glfwGetTime 等平台 API，
// 使 Application / TimeManager 等上层逻辑在 Android（无 GLFW）与桌面端行为一致。
// 数值语义与 glfwGetTime 对齐：进程启动以来的秒数（double）。

#include <chrono>

namespace BigHero::Time
{
// 稳定时钟秒数（进程内单调递增，浮点精度足够帧计时）
inline double NowSeconds()
{
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}
} // namespace BigHero::Time
