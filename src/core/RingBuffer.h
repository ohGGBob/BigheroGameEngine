#pragma once
// 通用环形缓冲区（RingBuffer<T>）：固定容量、先进先出（FIFO）。
// 纯标准库、仅头文件，可离线单测。
//
// 商业化价值：用于输入事件队列、音频采样缓冲、帧时间历史、渲染命令提交队列、
// 以及任何"容量受限、先进先出、需避免 vector 头部搬移"的场景。
//
// 设计：
//   - 连续数组 + head/tail 游标 + 元素计数。空/满由 size_ 判定，避免 head==tail 歧义。
//   - push_back：满时返回 false 且不覆盖（区别于覆盖式写）。需覆盖可先 pop_front。
//   - pop_front：空时返回 false；非空时移动弹出（零额外拷贝）。
//   - Front()/Back()：只读访问两端。
//   - operator[](i)：按 FIFO 顺序索引，0=最旧（用于遍历/采样）。
//   - Reserve/Capacity/Size/Empty/Full/Clear。

#include <cstddef>
#include <utility>
#include <vector>

namespace BigHero::Core
{
template<typename T> class RingBuffer
{
  public:
    explicit RingBuffer(size_t capacity = 0) { Reserve(capacity); }

    // 追加到尾部；满时返回 false 且不写入。
    bool PushBack(const T& value) noexcept
    {
        return PushBackImpl(value);
    }
    bool PushBack(T&& value) noexcept
    {
        return PushBackImpl(std::move(value));
    }

    // 从头部弹出。空时返回 false；非空时把元素移动到 out（零拷贝），成功返回 true。
    bool PopFront(T& out) noexcept
    {
        if (size_ == 0)
            return false;
        out = std::move(buf_[head_]);
        head_ = (head_ + 1) % capacity_;
        --size_;
        return true;
    }

    // 只读访问头部（最旧元素）。空时 UB，调用方保证非空。
    [[nodiscard]] const T& Front() const noexcept { return buf_[head_]; }
    // 只读访问尾部（最新元素）。空时 UB，调用方保证非空。
    [[nodiscard]] const T& Back() const noexcept { return buf_[(head_ + size_ - 1) % capacity_]; }

    // 按 FIFO 顺序索引：0=最旧，size-1=最新。越界为 UB，调用方保证合法。
    [[nodiscard]] const T& operator[](size_t i) const noexcept { return buf_[(head_ + i) % capacity_]; }

    [[nodiscard]] size_t Size() const noexcept { return size_; }
    [[nodiscard]] size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool Empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool Full() const noexcept { return size_ == capacity_; }

    // 清空（保留容量）。
    void Clear() noexcept
    {
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

    // 预分配容量。若当前 size 大于新容量，截断最旧元素至新容量。
    void Reserve(size_t capacity)
    {
        if (capacity == capacity_)
            return;
        std::vector<T> fresh(capacity);
        // 搬运现有元素（保序：旧→新）
        const size_t keep = size_ < capacity ? size_ : capacity;
        for (size_t i = 0; i < keep; ++i)
            fresh[i] = std::move(buf_[(head_ + i) % capacity_]);
        buf_ = std::move(fresh);
        head_ = 0;
        tail_ = keep % capacity;
        size_ = keep;
        capacity_ = capacity;
    }

  private:
    template<typename U> bool PushBackImpl(U&& value) noexcept
    {
        if (size_ == capacity_)
            return false;
        buf_[tail_] = std::forward<U>(value);
        tail_ = (tail_ + 1) % capacity_;
        ++size_;
        return true;
    }

    std::vector<T> buf_;
    size_t capacity_ = 0;
    size_t head_ = 0; // 头部（最旧）在 buf_ 的下标
    size_t tail_ = 0; // 下一个写入位置在 buf_ 的下标
    size_t size_ = 0;
};
} // namespace BigHero::Core
