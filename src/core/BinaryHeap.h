#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// BinaryHeap: a min-heap of (priority, id) pairs with decrease-key support.
// Standard-library only, self-contained.
class BinaryHeap {
public:
    BinaryHeap() {}
    explicit BinaryHeap(std::size_t capacity) { reserve(capacity); }

    void reserve(std::size_t n) {
        heap_.reserve(n); pos_.reserve(n);
    }
    void clear() { heap_.clear(); pos_.assign(pos_.size(), -1); }
    std::size_t size() const { return heap_.size(); }
    bool empty() const { return heap_.empty(); }

    bool contains(unsigned id) const {
        return id < pos_.size() && pos_[id] >= 0;
    }

    void push(unsigned id, float priority) {
        if (id >= pos_.size()) pos_.resize(id+1, -1);
        if (contains(id)) return;
        heap_.push_back({priority, id});
        pos_[id] = (int)heap_.size() - 1;
        siftUp((int)heap_.size() - 1);
    }

    std::pair<unsigned,float> top() const {
        return {heap_[0].id, heap_[0].priority};
    }

    void pop() {
        if (heap_.empty()) return;
        unsigned id = heap_[0].id;
        pos_[id] = -1;
        if (heap_.size() > 1) {
            heap_[0] = heap_.back();
            pos_[heap_[0].id] = 0;
            heap_.pop_back();
            siftDown(0);
        } else {
            heap_.pop_back();
        }
    }

    // Decrease key for an existing id (must be present).
    void decreaseKey(unsigned id, float newPriority) {
        if (!contains(id)) return;
        int i = pos_[id];
        if (newPriority >= heap_[i].priority) return;
        heap_[i].priority = newPriority;
        siftUp(i);
    }

private:
    struct Node { float priority; unsigned id; };
    std::vector<Node> heap_;
    std::vector<int> pos_;

    void siftUp(int i) {
        while (i > 0) {
            int p = (i - 1) / 2;
            if (heap_[p].priority <= heap_[i].priority) break;
            swap(p, i); i = p;
        }
    }
    void siftDown(int i) {
        int n = (int)heap_.size();
        while (true) {
            int l = 2*i+1, r = 2*i+2, best = i;
            if (l < n && heap_[l].priority < heap_[best].priority) best = l;
            if (r < n && heap_[r].priority < heap_[best].priority) best = r;
            if (best == i) break;
            swap(i, best); i = best;
        }
    }
    void swap(int i, int j) {
        std::swap(pos_[heap_[i].id], pos_[heap_[j].id]);
        std::swap(heap_[i], heap_[j]);
    }
};

} // namespace bighero
