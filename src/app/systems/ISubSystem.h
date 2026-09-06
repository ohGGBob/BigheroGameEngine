#pragma once
// 子系统接口：定义初始化/更新/销毁的通用契约
// 所有游戏玩法/工具子系统均实现此接口，由 Application 统一生命周期管理
//
// 商业化增强：
//   - SetEnabled()/Enabled()：运行时启用/禁用子系统的开关（默认启用）。被禁用的系统
//     Update() 仍会被调用（保持生命周期一致），但可在内部判断 Enabled() 提前返回，
//     用于在运行期暂停某系统而不必注销/重建（如调试时关闭粒子、暂停物理）。
//   - 非虚成员，无需子类重写；行为对所有实现一致。

#include <cstdint>
#include <functional>

namespace BigHero::App
{

struct FrameContext
{
    float deltaTime = 0.0f;
    float totalTime = 0.0f;
    uint32_t frameIndex = 0;
};

struct RenderContext
{
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
};

class ISubSystem
{
public:
    virtual ~ISubSystem() = default;

    // 初始化：在 Application 初始化资源后、创建管线前调用
    // 此时 Vulkan 设备、交换链、描述符管理器等已就绪
    virtual void Init() = 0;

    // 销毁：在 Application 析构前调用，释放 GPU 资源
    virtual void Shutdown() = 0;

    // 每帧更新：在 Application::Run 主循环 Update 阶段调用
    // dt 为帧时间（秒）
    virtual void Update(const FrameContext& frame) = 0;

    // 可选：每帧渲染前回调（用于更新 UBO、描述符等）
    // frameIndex 为当前帧并行槽位
    virtual void PreRender(uint32_t frameIndex) {}

    // 可选：交换链重建回调
    virtual void OnSwapchainRecreated() {}

    // 可选：渲染通道重建回调
    virtual void OnRenderPassRecreated() {}

    // 系统优先级：数值越小越早初始化/更新
    // 用于处理系统间依赖（如物理需先于角色控制器更新）
    [[nodiscard]] virtual int Priority() const noexcept { return 0; }

    // 系统名称（用于调试/Profiling）
    [[nodiscard]] virtual const char* Name() const noexcept = 0;

    // 运行时启用/禁用开关（默认启用）。
    // 被禁用的系统可选择在 Update() 中判断 Enabled() 后提前返回，实现运行期暂停。
    void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }

private:
    bool enabled_ = true;
};

} // namespace BigHero::App
