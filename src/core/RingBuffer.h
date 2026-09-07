#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// Fixed-capacity circular buffer (ring buffer).
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity = 64)
        : buf_(capacity), head_(0), tail_(0), count_(0) {}

    void Reserve(std::size_t n) { buf_.resize(n); if (count_ > n) count_ = n; }

    bool PushBack(const T& v) {
        if (count_ == buf_.size()) return false; // full
        buf_[tail_] = v;
        tail_ = (tail_ + 1) % buf_.size();
        ++count_;
        return true;
    }
    bool EmplaceBack(T&& v) { return PushBack(v); }

    bool PopFront(T& out) {
        if (count_ == 0) return false;
        out = buf_[head_];
        head_ = (head_ + 1) % buf_.size();
        --count_;
        return true;
    }

    void Clear() { head_ = tail_ = count_ = 0; }
    bool Empty() const { return count_ == 0; }
    bool Full() const { return count_ == buf_.size(); }
    std::size_t Size() const { return count_; }
    std::size_t Capacity() const { return buf_.size(); }

    T& Front() { return buf_[head_]; }
    const T& Front() const { return buf_[head_]; }
    T& Back() { return buf_[(tail_ + buf_.size() - 1) % buf_.size()]; }
    const T& Back() const { return buf_[(tail_ + buf_.size() - 1) % buf_.size()]; }

    T& operator[](std::size_t i) { return buf_[(head_ + i) % buf_.size()]; }
    const T& operator[](std::size_t i) const { return buf_[(head_ + i) % buf_.size()]; }

private:
    std::vector<T> buf_;
    std::size_t head_, tail_, count_;
};

} // namespace bighero
