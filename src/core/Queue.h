#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// FIFO queue backed by a ring buffer over a vector.
template <typename T>
class Queue {
public:
    explicit Queue(std::size_t capacity = 16) :
        data_(capacity < 1 ? 1 : capacity), head_(0), tail_(0), count_(0) {}

    void Reserve(std::size_t n) {
        if (n <= data_.size()) return;
        std::vector<T> nd(n);
        for (std::size_t i = 0; i < count_; ++i)
            nd[i] = data_[(head_ + i) % data_.size()];
        data_.swap(nd);
        head_ = 0; tail_ = count_;
    }

    bool Push(const T& v) {
        if (count_ == data_.size()) Reserve(data_.size() * 2);
        data_[tail_] = v;
        tail_ = (tail_ + 1) % data_.size();
        ++count_;
        return true;
    }

    bool Pop(T& out) {
        if (count_ == 0) return false;
        out = data_[head_];
        head_ = (head_ + 1) % data_.size();
        --count_;
        return true;
    }
    bool Pop() {
        if (count_ == 0) return false;
        head_ = (head_ + 1) % data_.size();
        --count_;
        return true;
    }

    T& Front() { return data_[head_]; }
    const T& Front() const { return data_[head_]; }
    std::size_t Size() const { return count_; }
    bool Empty() const { return count_ == 0; }
    void Clear() { head_ = tail_ = count_ = 0; }

private:
    std::vector<T> data_;
    std::size_t head_, tail_, count_;
};

} // namespace bighero
