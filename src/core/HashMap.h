#pragma once
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

namespace bighero {

// Simple hash map for small/medium collections (int -> int for simplicity),
// with linear probing. Kept dependency-free (no std::unordered_map).
class HashMap {
public:
    HashMap() : size_(0), capacity_(16), table_(capacity_ * 2, -1), values_(capacity_ * 2, 0), occupied_(capacity_, false) {}

    void Put(int key, int value) {
        if (size_ * 2 >= capacity_) Rehash(capacity_ * 2);
        uint32_t h = Hash(key);
        uint32_t mask = capacity_ - 1;
        uint32_t i = h & mask;
        for (uint32_t n = 0; n < capacity_; ++n) {
            uint32_t idx = (i + n) & mask;
            if (!occupied_[idx]) {
                occupied_[idx] = true;
                table_[idx] = key;
                values_[idx] = value;
                ++size_;
                return;
            }
            if (table_[idx] == key) { values_[idx] = value; return; }
        }
    }

    bool Get(int key, int& out) const {
        uint32_t h = Hash(key);
        uint32_t mask = capacity_ - 1;
        uint32_t i = h & mask;
        for (uint32_t n = 0; n < capacity_; ++n) {
            uint32_t idx = (i + n) & mask;
            if (!occupied_[idx]) return false;
            if (table_[idx] == key) { out = values_[idx]; return true; }
        }
        return false;
    }

    bool Contains(int key) const {
        int dummy;
        return Get(key, dummy);
    }

    bool Remove(int key) {
        uint32_t h = Hash(key);
        uint32_t mask = capacity_ - 1;
        uint32_t i = h & mask;
        for (uint32_t n = 0; n < capacity_; ++n) {
            uint32_t idx = (i + n) & mask;
            if (!occupied_[idx]) return false;
            if (table_[idx] == key) {
                // tombstone via rehash-free removal (mark empty + reinsert cluster)
                occupied_[idx] = false;
                --size_;
                Rehash(capacity_); // simplifies correctness; acceptable for our sizes
                return true;
            }
        }
        return false;
    }

    std::size_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }
    void Clear() { size_ = 0; occupied_.assign(capacity_, false); }

private:
    static uint32_t Hash(int k) {
        uint32_t x = (uint32_t)k;
        x = ((x >> 16) ^ x) * 0x45d9f3bu;
        x = ((x >> 16) ^ x) * 0x45d9f3bu;
        x = (x >> 16) ^ x;
        return x;
    }
    void Rehash(uint32_t newCap) {
        std::vector<int> oldTable = table_;
        std::vector<int> oldVals = values_;
        std::vector<bool> oldOcc = occupied_;
        uint32_t oldCap = capacity_;
        capacity_ = newCap < 16 ? 16 : newCap;
        table_.assign(capacity_ * 2, -1);
        values_.assign(capacity_ * 2, 0);
        occupied_.assign(capacity_, false);
        size_ = 0;
        for (uint32_t i = 0; i < oldCap; ++i)
            if (oldOcc[i]) Put(oldTable[i], oldVals[i]);
    }
    std::size_t size_;
    uint32_t capacity_;
    std::vector<int> table_;      // keys (capacity*2 safe indexing)
    std::vector<int> values_;     // values
    std::vector<bool> occupied_;
};

} // namespace bighero
