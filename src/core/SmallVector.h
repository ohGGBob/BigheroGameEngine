#pragma once
// 小缓冲向量（SmallVector）：容量小时用栈上内嵌缓冲，超出后转入堆分配。
// 纯标准库、仅头文件。
//
// 商业化价值：大量小型临时容器（顶点列表、遍历栈、局部缓冲）的高性能路径；
// 避免“小而高频”的堆分配，同时兼容常见容器接口习惯。
//
// 设计：容量 <= N 用内嵌 std::array；溢出后迁移到 std::vector<T> 堆存储。
//   - 用显式 onHeap_ 标志记录当前存储阶段，ptr() 严格依据该标志取址，
//     避免“迁移到堆后又按 size<=N 回退读取已被移动搬空的内嵌缓冲”的悬垂读取。
//   - Clear()/pop_back() 归零时会同步清空堆并复位 onHeap_，防止残留状态。
// 迭代器统一返回 T*（指针），无论在 inline 还是 heap 阶段都稳定。

#include <array>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace BigHero::Core
{
template<typename T, size_t N> class SmallVector
{
  public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    [[nodiscard]] size_t Size() const { return size_; }
    [[nodiscard]] bool Empty() const { return size_ == 0; }
    // 当前是否已处于堆存储阶段（内嵌缓冲已迁移到 heap_）。
    [[nodiscard]] bool OnHeap() const { return onHeap_; }

    void Clear()
    {
        if (onHeap_)
        {
            heap_.clear();
            onHeap_ = false;
        }
        size_ = 0;
    }

    // 若仍在 inline 阶段且有空间则写入内嵌缓冲；否则（必要时先迁移）委托给 heap vector。
    void push_back(const T& v)
    {
        if (!onHeap_ && size_ < N)
        {
            inline_[size_++] = v;
            return;
        }
        if (!onHeap_)
            MigrateToHeap();
        heap_.push_back(v);
        ++size_;
    }
    void push_back(T&& v)
    {
        if (!onHeap_ && size_ < N)
        {
            inline_[size_++] = std::move(v);
            return;
        }
        if (!onHeap_)
            MigrateToHeap();
        heap_.push_back(std::move(v));
        ++size_;
    }
    void pop_back()
    {
        if (size_ == 0)
            return;
        if (onHeap_)
            heap_.pop_back();
        --size_;
        // 归零时收缩回内嵌阶段，保证后续 Clear()[再 push] 状态干净。
        if (onHeap_ && size_ == 0)
        {
            heap_.clear();
            onHeap_ = false;
        }
    }

    T& At(size_t i)
    {
        if (i >= size_)
            throw std::out_of_range("SmallVector index");
        return ptr()[i];
    }
    const T& At(size_t i) const { return const_cast<SmallVector*>(this)->At(i); }
    T& operator[](size_t i) { return ptr()[i]; }
    const T& operator[](size_t i) const { return const_cast<SmallVector*>(this)->ptr()[i]; }

    T& Front() { return ptr()[0]; }
    const T& Front() const { return ptr()[0]; }
    T& Back() { return ptr()[size_ - 1]; }
    const T& Back() const { return ptr()[size_ - 1]; }

    iterator begin() { return ptr(); }
    iterator end() { return ptr() + size_; }
    const_iterator begin() const { return ptr(); }
    const_iterator end() const { return ptr() + size_; }

  private:
    // 把内嵌缓冲中的元素 move 到堆，并切换到堆存储阶段（此后指针以 heap_ 为准）。
    void MigrateToHeap()
    {
        heap_.reserve(N * 2);
        for (size_t i = 0; i < size_; ++i)
            heap_.push_back(std::move(inline_[i]));
        onHeap_ = true;
    }
    T* ptr() { return onHeap_ ? heap_.data() : inline_.data(); }
    const T* ptr() const { return onHeap_ ? heap_.data() : inline_.data(); }

    std::array<T, N> inline_{};
    size_t size_ = 0;
    bool onHeap_ = false;
    std::vector<T> heap_;
};
} // namespace BigHero::Core
