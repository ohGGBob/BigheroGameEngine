#pragma once
// 小缓冲向量（SmallVector）：容量小时用栈上内嵌缓冲，超出后转入堆分配。
// 纯标准库、仅头文件。
//
// 商业化价值：大量小型临时容器（顶点列表、遍历栈、局部缓冲）的高性能路径；
// 避免"小而高频"的堆分配，同时兼容常见容器接口习惯。
//
// 注意：SmallVector 内部可能持有小缓冲或堆指针，拷贝/移动语义需正确管理。
// 设计：若 size <= N 用内嵌 std::array；否则用 std::vector<T>（更简单可靠的实现）。
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
    void Clear() { size_ = 0; }

    // 若处于 inline 阶段直接写入内嵌缓冲；否则委托给 heap vector。
    void push_back(const T& v)
    {
        if (size_ < N)
        {
            inline_[size_++] = v;
        }
        else
        {
            Heap().push_back(v);
            ++size_;
        }
    }
    void push_back(T&& v)
    {
        if (size_ < N)
        {
            inline_[size_++] = std::move(v);
        }
        else
        {
            Heap().push_back(std::move(v));
            ++size_;
        }
    }
    void pop_back()
    {
        if (size_ < N)
            --size_;
        else
        {
            heap_.pop_back();
            --size_;
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
    const T& operator[](size_t i) const { return const_cast<SmallVector*>(this)->operator[](i); }

    iterator begin() { return ptr(); }
    iterator end() { return ptr() + size_; }
    const_iterator begin() const { return ptr(); }
    const_iterator end() const { return ptr() + size_; }

  private:
    T* ptr()
    {
        return size_ <= N ? inline_.data() : heap_.data();
    }
    const T* ptr() const
    {
        return size_ <= N ? inline_.data() : heap_.data();
    }
    std::vector<T>& Heap()
    {
        if (heap_.empty() && size_ > 0)
        {
            heap_.reserve(N * 2);
            for (size_t i = 0; i < size_; ++i)
                heap_.push_back(std::move(inline_[i]));
        }
        return heap_;
    }

    std::array<T, N> inline_{};
    size_t size_ = 0;
    std::vector<T> heap_;
};
} // namespace BigHero::Core
