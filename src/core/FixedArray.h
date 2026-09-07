#pragma once
// 固定容量数组（FixedArray）：栈上固定容量、无动态分配的紧凑容器。
// 纯标准库、仅头文件。
//
// 商业化价值：渲染批次、粒子池、固定大小缓冲等"容量已知且不大"的高频路径；
// 消除堆分配，改善缓存局部性，适合当作 POD 常驻缓冲。
//
// 提供：push_back/pop_back/At/operator[]/Clear/Size/Capacity/Full/Empty/begin/end。
// 容量编译期固定（N），不扩容；超出容量 push_back 抛异常（Debug 断言可改）。

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <array>

namespace BigHero::Core
{
template<typename T, size_t N> class FixedArray
{
  public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    [[nodiscard]] constexpr size_t Size() const { return size_; }
    [[nodiscard]] constexpr size_t Capacity() const { return N; }
    [[nodiscard]] bool Empty() const { return size_ == 0; }
    [[nodiscard]] bool Full() const { return size_ == N; }
    void Clear() { size_ = 0; }

    void push_back(const T& v)
    {
        if (Full())
            throw std::overflow_error("FixedArray capacity exceeded");
        data_[size_++] = v;
    }
    void push_back(T&& v)
    {
        if (Full())
            throw std::overflow_error("FixedArray capacity exceeded");
        data_[size_++] = std::move(v);
    }
    void pop_back()
    {
        if (Empty())
            throw std::underflow_error("FixedArray pop_back on empty");
        --size_;
    }

    T& At(size_t i)
    {
        if (i >= size_)
            throw std::out_of_range("FixedArray index out of range");
        return data_[i];
    }
    const T& At(size_t i) const
    {
        if (i >= size_)
            throw std::out_of_range("FixedArray index out of range");
        return data_[i];
    }
    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }
    T& Back() { return data_[size_ - 1]; }
    const T& Back() const { return data_[size_ - 1]; }

    iterator begin() { return data_.data(); }
    const_iterator begin() const { return data_.data(); }
    iterator end() { return data_.data() + size_; }
    const_iterator end() const { return data_.data() + size_; }

  private:
    std::array<T, N> data_{};
    size_t size_ = 0;
};
} // namespace BigHero::Core
