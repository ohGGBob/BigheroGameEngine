#pragma once
#include <cstdint>
#include <vector>
#include <atomic>

namespace bighero {

// RingBuffer: a fixed-capacity single-producer/single-consumer byte ring that
// never allocates after construction. Sqrt-free, lock-free for SPSC via atomic
// head/tail indices. Self-contained, std-lib only.
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) : buf_(capacity), mask_(capacity - 1) {}
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    size_t Capacity() const { return buf_.size(); }
    // Must be power of two for mask-based wrap.
    bool IsPowerOfTwo() const { return buf_.size() && ((buf_.size() & (buf_.size()-1)) == 0); }

    size_t Size() const {
        size_t w = head_.load(std::memory_order_acquire);
        size_t r = tail_.load(std::memory_order_acquire);
        return w - r;
    }
    bool Empty() const { return Size() == 0; }
    bool Full() const { return Size() == buf_.size(); }
    size_t Free() const { return buf_.size() - Size(); }

    // Write bytes; returns number actually written (may be less than n if full).
    size_t Write(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        size_t written = 0;
        for (size_t i = 0; i < n; ++i) {
            if (Full()) break;
            size_t w = head_.load(std::memory_order_relaxed);
            buf_[w & mask_] = p[i];
            head_.store(w + 1, std::memory_order_release);
            ++written;
        }
        return written;
    }

    // Read up to n bytes into out; returns number read.
    size_t Read(void* out, size_t n) {
        uint8_t* p = static_cast<uint8_t*>(out);
        size_t read = 0;
        for (size_t i = 0; i < n; ++i) {
            if (Empty()) break;
            size_t r = tail_.load(std::memory_order_relaxed);
            p[i] = buf_[r & mask_];
            tail_.store(r + 1, std::memory_order_release);
            ++read;
        }
        return read;
    }

    uint8_t Peek() const {
        size_t r = tail_.load(std::memory_order_acquire);
        return buf_[r & mask_];
    }
    void Clear() { head_.store(0); tail_.store(0); }

private:
    std::vector<uint8_t> buf_;
    size_t mask_ = 0;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

} // namespace bighero
