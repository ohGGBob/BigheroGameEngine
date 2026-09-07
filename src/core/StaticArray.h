#pragma once
#include <cstddef>
#include <cstring>

namespace bighero {

// Fixed-capacity array (no heap). Bound-checked access in debug; direct in release.
template <typename T, std::size_t N>
class StaticArray {
public:
    using value_type = T;

    StaticArray() : size_(0) {}

    bool PushBack(const T& v) {
        if (size_ >= N) return false;
        data_[size_++] = v;
        return true;
    }

    void PopBack() { if (size_ > 0) --size_; }

    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }

    T& At(std::size_t i) { return data_[i]; }
    const T& At(std::size_t i) const { return data_[i]; }

    T* Begin() { return data_; }
    T* End() { return data_ + size_; }
    const T* Begin() const { return data_; }
    const T* End() const { return data_ + size_; }

    std::size_t Size() const { return size_; }
    static constexpr std::size_t Capacity() { return N; }
    bool Empty() const { return size_ == 0; }
    bool Full() const { return size_ == N; }
    void Clear() { size_ = 0; }
    T& Back() { return data_[size_ - 1]; }
    const T& Back() const { return data_[size_ - 1]; }

private:
    T data_[N];
    std::size_t size_;
};

} // namespace bighero
