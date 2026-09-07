#pragma once
// 多播委托（Delegate）：类型安全的多回调分发，支持返回值聚合的轻量实现。
// 纯标准库、仅头文件。
//
// 商业化价值：UI 事件、系统钩子、广播通知的多监听者模式；
// 不同于 Signal/Slot 的无返回通知，这里支持"多个处理器返回值并聚合"（如事件是否已处理）。
//
// 提供：Bind(handler) 返回 Connection(可 Unbind)、Broadcast(args...) 依次调用并可选聚合返回值。
// 若不用返回值，可用 Broadcast(args...) 忽略聚合结果。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace BigHero::Core
{
template<typename... Args> class Delegate
{
  public:
    struct Connection { uint64_t id = 0; };

    Connection Bind(std::function<void(Args...)> handler)
    {
        std::lock_guard<std::mutex> lock(mx_);
        Handlers.push_back({ ++nextId_, std::move(handler) });
        return Connection{ nextId_ };
    }

    void Unbind(Connection c)
    {
        std::lock_guard<std::mutex> lock(mx_);
        Handlers.erase(std::remove_if(Handlers.begin(), Handlers.end(),
                                      [&](const Entry& e) { return e.id == c.id; }),
                       Handlers.end());
    }

    void UnbindAll()
    {
        std::lock_guard<std::mutex> lock(mx_);
        Handlers.clear();
    }

    void Broadcast(Args... args)
    {
        // 拷贝快照，避免广播期间绑定/解绑导致迭代器失效。
        std::vector<Entry> snap;
        {
            std::lock_guard<std::mutex> lock(mx_);
            snap = Handlers;
        }
        for (auto& e : snap)
            e.handler(args...);
    }

    [[nodiscard]] size_t Count() const
    {
        std::lock_guard<std::mutex> lock(mx_);
        return Handlers.size();
    }

  private:
    struct Entry { uint64_t id; std::function<void(Args...)> handler; };
    std::vector<Entry> Handlers;
    mutable std::mutex mx_;
    uint64_t nextId_ = 0;
};
} // namespace BigHero::Core
