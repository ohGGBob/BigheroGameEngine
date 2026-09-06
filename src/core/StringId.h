#pragma once
// 字符串驻留池（StringId / StringDatabase）：把字符串映射为稳定整数 ID，并全局注册。
// 纯标准库、仅头文件、跨线程互斥保护。
//
// 商业化价值：资源名/材质名/动画名/事件名的 ID 化查找；用整数比较代替每次 strcmp，
// 是商业引擎"字符串 -> ID"的标准做法（类 EnTT hashed_string / Unity 的 name hash）。
//
// 设计：
//   - Intern(name)：返回稳定 32 位 ID（FNV-1a 哈希）。同名 → 同 ID。
//   - IsValid/Resolve(id)：ID → 字符串名（需先 Intern 注册）。
//   - 线程安全：全局哈希表 + 互斥锁保护（ID 与 name 双向映射）。
//   - 复用 hash::Fnv1a32 以保持一致性。
//
// 注意：ID 是 32 位哈希，理论上存在碰撞可能；对绝大多数运行时查找安全。

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <string_view>

#include "HashUtils.h" // 提供 hash::Fnv1a32

namespace BigHero::Core
{
class StringId
{
  public:
    using Id = uint32_t;
    static constexpr Id kInvalid = 0;

    // 把字符串注册并返回稳定 ID（同名重复返回同一 ID）。thread-safe。
    static Id Intern(std::string_view name)
    {
        if (name.empty())
            return kInvalid;
        const Id id = hash::Fnv1a32(name, 0x811c9dc5u);
        if (id == kInvalid)
            return kInvalid; // 保留 0 为非法；极罕见碰撞时拒绝（调用方可重试加盐）
        std::lock_guard<std::mutex> lock(Mutex());
        NameMap()[id] = std::string(name);
        IdMap()[std::string(name)] = id;
        return id;
    }

    // 是否已注册该字符串。
    static bool IsInterned(std::string_view name)
    {
        if (name.empty())
            return false;
        std::lock_guard<std::mutex> lock(Mutex());
        return IdMap().count(std::string(name)) > 0;
    }

    // 查询 ID 对应的字符串；未注册返回空串。
    static std::string_view Resolve(Id id) noexcept
    {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto it = NameMap().find(id);
        return it != NameMap().end() ? std::string_view(it->second) : std::string_view{};
    }

  private:
    static std::unordered_map<Id, std::string>& NameMap()
    {
        static std::unordered_map<Id, std::string> m;
        return m;
    }
    static std::unordered_map<std::string, Id>& IdMap()
    {
        static std::unordered_map<std::string, Id> m;
        return m;
    }
    static std::mutex& Mutex()
    {
        static std::mutex m;
        return m;
    }
};
} // namespace BigHero::Core
