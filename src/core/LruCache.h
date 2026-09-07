#pragma once
#include <cstddef>
#include <list>
#include <unordered_map>
#include <utility>

namespace bighero {

// Simple LRU cache keyed by K -> V with a bounded capacity.
template <typename K, typename V>
class LruCache {
public:
    explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

    void SetCapacity(std::size_t cap) {
        capacity_ = cap;
        while (order_.size() > capacity_) EvictOldest();
    }

    std::size_t Capacity() const { return capacity_; }
    std::size_t Size() const { return order_.size(); }

    void Put(const K& key, const V& value) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            order_.erase(it->second.second);
            order_.push_front(key);
            it->second.second = order_.begin();
            it->second.first = value;
            return;
        }
        order_.push_front(key);
        map_[key] = std::make_pair(value, order_.begin());
        if (order_.size() > capacity_) EvictOldest();
    }

    bool Get(const K& key, V& out) {
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        order_.erase(it->second.second);
        order_.push_front(key);
        it->second.second = order_.begin();
        out = it->second.first;
        return true;
    }

    bool Contains(const K& key) const { return map_.find(key) != map_.end(); }

    void Erase(const K& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return;
        order_.erase(it->second.second);
        map_.erase(it);
    }

    void Clear() { map_.clear(); order_.clear(); }

private:
    void EvictOldest() {
        if (order_.empty()) return;
        const K& last = order_.back();
        auto it = map_.find(last);
        if (it != map_.end()) map_.erase(it);
        order_.pop_back();
    }

    std::size_t capacity_;
    std::list<K> order_;
    std::unordered_map<K, std::pair<V, typename std::list<K>::iterator>> map_;
};

} // namespace bighero
