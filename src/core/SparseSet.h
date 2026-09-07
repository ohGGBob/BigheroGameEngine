#pragma once
// 稀疏集合（SparseSet）：支持紧凑迭代、O(1) 插入/删除/查询的集合。
// 纯标准库、仅头文件。
//
// 商业化价值：ECS 实体/组件表、渲染对象集、碰撞体集合的典型底层结构；
// 比哈希表迭代更线性（dense 数组保持插入序），删除 O(1) 且迭代无空洞。

#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace BigHero::Core
{
template<typename Key> class SparseSet
{
  public:
    SparseSet() = default;

    // 若 key 存在返回 false；否则插入并返回 true。
    bool Insert(const Key& k)
    {
        if (Contains(k))
            return false;
        int idx = (int)dense_.size();
        dense_.push_back(k);
        sparse_[k] = idx;
        return true;
    }

    bool Erase(const Key& k)
    {
        auto it = sparse_.find(k);
        if (it == sparse_.end())
            return false;
        int idx = it->second;
        int last = (int)dense_.size() - 1;
        if (idx != last)
        {
            // 把最后一个元素移到被删位置（swap-erase 保持紧凑）
            dense_[idx] = dense_[last];
            sparse_[dense_[idx]] = idx;
        }
        dense_.pop_back();
        sparse_.erase(it);
        return true;
    }

    [[nodiscard]] bool Contains(const Key& k) const { return sparse_.find(k) != sparse_.end(); }
    [[nodiscard]] size_t Size() const { return dense_.size(); }
    [[nodiscard]] bool Empty() const { return dense_.empty(); }
    void Clear()
    {
        dense_.clear();
        sparse_.clear();
    }

    const std::vector<Key>& Dense() const { return dense_; }
    // 获取 key 对应 dense 索引（不存在返回 -1）。
    int IndexOf(const Key& k) const
    {
        auto it = sparse_.find(k);
        return it == sparse_.end() ? -1 : it->second;
    }

    auto begin() { return dense_.begin(); }
    auto end() { return dense_.end(); }
    auto begin() const { return dense_.begin(); }
    auto end() const { return dense_.end(); }

  private:
    std::vector<Key> dense_;
    std::unordered_map<Key, int> sparse_;
};
} // namespace BigHero::Core
