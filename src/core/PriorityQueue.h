#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>
#include <functional>

namespace bighero {

// Min-priority queue (smallest value first) over a binary heap.
template <typename T, typename Compare = std::less<T>>
class PriorityQueue {
public:
    explicit PriorityQueue(Compare cmp = Compare()) : cmp_(cmp), heap_(0) {}

    void Push(const T& v) {
        heap_.push_back(v);
        SiftUp(heap_.size() - 1);
    }

    bool Pop(T& out) {
        if (heap_.empty()) return false;
        out = heap_[0];
        heap_[0] = heap_.back();
        heap_.pop_back();
        if (!heap_.empty()) SiftDown(0);
        return true;
    }

    const T& Top() const { return heap_.front(); }
    std::size_t Size() const { return heap_.size(); }
    bool Empty() const { return heap_.empty(); }
    void Clear() { heap_.clear(); }

private:
    void SiftUp(std::size_t i) {
        while (i > 0) {
            std::size_t p = (i - 1) / 2;
            if (cmp_(heap_[i], heap_[p])) { std::swap(heap_[i], heap_[p]); i = p; }
            else break;
        }
    }
    void SiftDown(std::size_t i) {
        std::size_t n = heap_.size();
        while (true) {
            std::size_t smallest = i;
            std::size_t l = 2*i + 1, r = 2*i + 2;
            if (l < n && cmp_(heap_[l], heap_[smallest])) smallest = l;
            if (r < n && cmp_(heap_[r], heap_[smallest])) smallest = r;
            if (smallest == i) break;
            std::swap(heap_[i], heap_[smallest]);
            i = smallest;
        }
    }
    Compare cmp_;
    std::vector<T> heap_;
};

} // namespace bighero
