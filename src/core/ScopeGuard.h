#pragma once
// 作用域守卫（ScopeGuard）：RAII，作用域退出时执行回调。
// 纯标准库、仅头文件。
//
// 商业化价值：异常安全的基本原语——资源回收、状态恢复、锁释放、日志收尾
// 都可用它保证"无论正常返回或异常退出都执行"。
//
// 提供：MakeScopeGuard(fn) / MakeScopeExit(fn)（退出时执行）、Dismiss() 取消执行。
// 也提供一个"始终执行"的 ScopeGuardOnExit 便捷类。

#include <utility>
#include <functional>

namespace BigHero::Core
{
template<typename F> class ScopeGuardImpl
{
  public:
    explicit ScopeGuardImpl(F fn) : fn_(std::move(fn)) {}
    ScopeGuardImpl(const ScopeGuardImpl&) = delete;
    ScopeGuardImpl& operator=(const ScopeGuardImpl&) = delete;
    ScopeGuardImpl(ScopeGuardImpl&& o) noexcept : fn_(std::move(o.fn_)), active_(o.active_) { o.active_ = false; }
    ScopeGuardImpl& operator=(ScopeGuardImpl&&) = delete;

    ~ScopeGuardImpl()
    {
        if (active_)
            fn_();
    }

    void Dismiss() { active_ = false; }
    [[nodiscard]] bool Active() const { return active_; }

  private:
    F fn_;
    bool active_ = true;
};

// 作用域退出时执行（无论正常/异常）。
template<typename F> ScopeGuardImpl<std::decay_t<F>> MakeScopeGuard(F&& fn)
{
    return ScopeGuardImpl<std::decay_t<F>>(std::forward<F>(fn));
}

// 语义别名：表明"退出时"执行。
template<typename F> ScopeGuardImpl<std::decay_t<F>> MakeScopeExit(F&& fn)
{
    return ScopeGuardImpl<std::decay_t<F>>(std::forward<F>(fn));
}
} // namespace BigHero::Core
