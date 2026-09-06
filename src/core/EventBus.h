#pragma once
// 事件总线（EventBus）：类型安全的事件注册/发布/派发。
// 纯标准库、仅头文件，支持任意事件类型（Key=事件类型），多监听者。
//
// 商业化价值：游戏逻辑解耦——输入/物理/网络/UI 各系统通过事件通信，避免子系统强依赖；
// 是商业化引擎事件系统（类似 UE4 的 FMessageBus / Unity 的 EventManager）的基础原语。
//
// 设计：
//   - 事件类型作为 Key（如 int/enum/自定义小结构），Event = 携带数据的负载结构。
//   - Subscribe：注册回调（返回句柄，可用于 Unsubscribe）。
//   - Publish：同步按注册顺序派发给该类型的所有监听者。
//   - Unsubscribe：按句柄移除。回调接收 const Event&。
//
// 注意：Publish 时若回调内部再次 Subscribe/Unsubscribe，使用拷贝快照派发以避免迭代器失效。
// 回调通过 shared_ptr<bool> 标记存活，保证派发期间被 Unsubscribe 的监听者其回调对象仍安全。

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <unordered_map>
#include <vector>

namespace BigHero::Core
{
template<typename Key, typename Event> class EventBus
{
  public:
    using Callback = std::function<void(const Event&)>;
    struct Handle { uint64_t id = 0; };

    Handle Subscribe(Key key, Callback cb)
    {
        std::lock_guard<std::mutex> lock(mx_);
        Listener l;
        l.cb = std::move(cb);
        l.alive = std::make_shared<bool>(true);
        l.id = ++nextId_;
        Map()[std::move(key)].push_back(l);
        return Handle{ l.id };
    }

    void Publish(const Key& key, const Event& ev)
    {
        // 拷贝快照，避免派发期间订阅/退订导致迭代器失效，并保证回调对象存活。
        std::vector<std::pair<Callback, std::weak_ptr<bool>>> snapshot;
        {
            std::lock_guard<std::mutex> lock(mx_);
            const auto it = Map().find(key);
            if (it == Map().end())
                return;
            snapshot.reserve(it->second.size());
            for (const auto& l : it->second)
                snapshot.emplace_back(l.cb, l.alive);
        }
        for (auto& s : snapshot)
        {
            if (auto alive = s.second.lock())
                s.first(ev);
        }
    }

    void Unsubscribe(Key key, Handle h)
    {
        std::lock_guard<std::mutex> lock(mx_);
        auto it = Map().find(key);
        if (it == Map().end())
            return;
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [&](const Listener& l) { return l.id == h.id; }),
                  vec.end());
    }

    void Clear(Key key)
    {
        std::lock_guard<std::mutex> lock(mx_);
        Map().erase(key);
    }

  private:
    struct Listener
    {
        Callback cb;
        uint64_t id = 0;
        std::shared_ptr<bool> alive;
    };
    static std::unordered_map<Key, std::vector<Listener>>& Map()
    {
        static std::unordered_map<Key, std::vector<Listener>> m;
        return m;
    }
    static std::mutex& Mutex()
    {
        static std::mutex m;
        return m;
    }
    uint64_t nextId_ = 0;
    std::mutex mx_;
};
} // namespace BigHero::Core
