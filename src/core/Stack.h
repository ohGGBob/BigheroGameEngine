#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// LIFO stack backed by a vector.
template <typename T>
class Stack {
public:
    void Push(const T& v) { data_.push_back(v); }
    void Emplace(T&& v) { data_.push_back(std::move(v)); }

    bool Pop(T& out) {
        if (data_.empty()) return false;
        out = data_.back();
        data_.pop_back();
        return true;
    }
    bool Pop() {
        if (data_.empty()) return false;
        data_.pop_back();
        return true;
    }

    T& Top() { return data_.back(); }
    const T& Top() const { return data_.back(); }
    std::size_t Size() const { return data_.size(); }
    bool Empty() const { return data_.empty(); }
    void Clear() { data_.clear(); }
    void Reserve(std::size_t n) { data_.reserve(n); }

private:
    std::vector<T> data_;
};

} // namespace bighero
