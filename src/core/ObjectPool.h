#pragma once
// 通用对象池（ObjectPool<T>）：预先分配并复用对象，避免运行时反复 new/delete。
// 纯标准库、仅头文件、可离线单测。
//
// 商业化价值：粒子系统、弹体、UI 节点、渲染 draw 项等"短寿命高频对象"的
// 分配热点；对象池显著降低分配开销与内存碎片，是商业引擎标配基础设施。
//
// 设计：
//   - 容量固定（构造时指定）。池内对象以 Slot{used/值} 存储，chunk 化 vector 保持连续。
//   - Acquire()：取空闲槽位（栈式 freeList 复用），返回 T* 并标记 used。
//     池满返回 nullptr（调用方需处理或换更大池）。
//   - Release(T*)：归还槽位并从池的 freeList 复用；非池内对象/重复释放为未定义行为。
//   - ForEach / Size / Capacity / UsedCount / FreeCount / Clear。
//   - release 后不析构对象（保留以便复用），对象生命周期由池管理。

#include <cstddef>
#include <vector>

namespace BigHero::Core
{
template<typename T> class ObjectPool
{
  public:
    explicit ObjectPool(size_t capacity = 0)
    {
        slots_.resize(capacity);
        freeList_.reserve(capacity);
        for (size_t i = capacity; i > 0; --i)
            freeList_.push_back(i - 1);
    }

    // 取一个空闲对象；池满返回 nullptr。
    T* Acquire()
    {
        if (freeList_.empty())
            return nullptr;
        const size_t idx = freeList_.back();
        freeList_.pop_back();
        slots_[idx].used = true;
        return &slots_[idx].value;
    }

    // 归还对象到池复用。重复 Release / 非池内对象行为未定义。
    // 注意：T* 指向的是 Slot::value，槽位间距为 sizeof(Slot)（含 bool used 填充），
    // 因此必须用字节偏移除以 sizeof(Slot) 得到槽位索引，而不能用 T* 相减（间距是 sizeof(T)）。
    void Release(T* ptr) noexcept
    {
        if (ptr == nullptr)
            return;
        const char* base = reinterpret_cast<const char*>(&slots_[0].value);
        const char* p = reinterpret_cast<const char*>(ptr);
        const size_t idx = static_cast<size_t>((p - base) / sizeof(Slot));
        slots_[idx].used = false;
        freeList_.push_back(idx);
    }

    // 遍历所有已分配（used）对象，fn(T&) 提供可变引用。
    template<typename F> void ForEach(F&& fn)
    {
        for (auto& s : slots_)
            if (s.used)
                fn(s.value);
    }
    template<typename F> void ForEach(F&& fn) const
    {
        for (const auto& s : slots_)
            if (s.used)
                fn(s.value);
    }

    [[nodiscard]] size_t Capacity() const noexcept { return slots_.size(); }
    [[nodiscard]] size_t UsedCount() const noexcept { return slots_.size() - freeList_.size(); }
    [[nodiscard]] size_t FreeCount() const noexcept { return freeList_.size(); }

    // 预分配确认（Acquire 前可选调用）：把池容量扩到至少 n。
    // 注意：扩容会重置所有槽位为"未使用"（清空当前所有对象）。
    void Reserve(size_t n)
    {
        if (n <= slots_.size())
            return;
        slots_.resize(n);
        freeList_.clear();
        for (size_t i = n; i > 0; --i)
            freeList_.push_back(i - 1);
    }

    // 清空全部对象（所有槽位标记未使用）。
    void Clear() noexcept
    {
        freeList_.clear();
        for (size_t i = slots_.size(); i > 0; --i)
            freeList_.push_back(i - 1);
        for (auto& s : slots_)
            s.used = false;
    }

  private:
    struct Slot
    {
        bool used = false;
        T value{};
    };
    std::vector<Slot> slots_;
    std::vector<size_t> freeList_;
};
} // namespace BigHero::Core
