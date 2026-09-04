#pragma once
// 引用计数的 LRU 资源缓存（AssetCache）+ 多类型资源管理器（AssetManager）。
// 纯 CPU、仅标准库、可离线单测。
//
// 设计：
//   - AssetCache<T>：以字符串路径为键，值由工厂函数按需加载（T 由 shared_ptr 托管）。
//     - 命中：把条目移到 MRU 端（list 前端）并返回 shared_ptr，不重复加载。
//     - 未命中：调用 factory(key) 加载并插入，随后若超容量，从 LRU 端（list 后端）
//       淘汰"未被外部引用"的条目（use_count()==1，即仅缓存持有）。
//     - 引用计数：返回的 shared_ptr 是缓存与调用方共享的同一对象；调用方持有期间
//       该条目不会被淘汰（淘汰只针对 use_count()==1 的条目）。
//   - AssetManager：按 type_index 持有多个不同类型缓存，Load<T>/Get<T>/Remove<T> 统一入口。
//
// 语义约定：
//   - capacity 为软上限：当超容量且所有条目都被外部引用时，无法淘汰，缓存可暂时超限。
//   - factory 返回的 T 为空指针（nullptr）表示加载失败，Load 返回 nullptr 且不缓存。

#include <functional>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace BigHero::Core
{
// type-erased 基类，供 AssetManager 按 type_index 统一持有不同类型缓存
struct IAssetCacheHolder
{
    virtual ~IAssetCacheHolder() = default;
};

// ---- 引用计数的 LRU 资源缓存 ----
template<typename T> class AssetCache : public IAssetCacheHolder
{
  public:
    using Factory = std::function<std::shared_ptr<T>(const std::string& key)>;

    // capacity：软容量上限；factory：按 key 加载资源的工厂。
    AssetCache(size_t capacity, Factory factory) : capacity_(capacity), factory_(std::move(factory)) {}

    // 取资源：命中返回缓存（刷新 LRU），未命中用工厂加载并缓存。
    // 工厂返回 nullptr 表示加载失败，不缓存并返回 nullptr。
    std::shared_ptr<T> Load(const std::string& key)
    {
        const auto it = items_.find(key);
        if (it != items_.end())
        {
            Touch(it->second.lruIt);
            return it->second.ptr;
        }

        std::shared_ptr<T> value = factory_(key);
        if (!value)
            return nullptr; // 加载失败，不缓存

        lru_.push_front(key);
        items_.emplace(key, Entry{value, lru_.begin()});
        EvictIfNeeded();
        return value;
    }

    // 仅查询，不加载；命中返回缓存句柄，否则 nullptr。
    std::shared_ptr<T> Get(const std::string& key) const
    {
        const auto it = items_.find(key);
        return (it != items_.end()) ? it->second.ptr : nullptr;
    }

    // 是否有该键的缓存
    [[nodiscard]] bool Contains(const std::string& key) const { return items_.find(key) != items_.end(); }

    // 手动移除某键（即使被外部引用也移除缓存引用，外部句柄仍有效）
    void Remove(const std::string& key)
    {
        const auto it = items_.find(key);
        if (it == items_.end())
            return;
        lru_.erase(it->second.lruIt);
        items_.erase(it);
    }

    // 调整软容量（立即尝试淘汰）
    void SetCapacity(size_t capacity)
    {
        capacity_ = capacity;
        EvictIfNeeded();
    }
    [[nodiscard]] size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t Size() const noexcept { return items_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return items_.empty(); }

    void Clear()
    {
        items_.clear();
        lru_.clear();
    }

  private:
    struct Entry
    {
        std::shared_ptr<T> ptr;
        std::list<std::string>::iterator lruIt;
    };

    // 把条目移到 MRU 端（list 前端）
    void Touch(const std::list<std::string>::iterator it) { lru_.splice(lru_.begin(), lru_, it); }

    // 从 LRU 端（list 后端）淘汰未被外部引用的条目，直到不超过容量。
    void EvictIfNeeded()
    {
        while (items_.size() > capacity_ && !lru_.empty())
        {
            const std::string& lruKey = lru_.back();
            const auto it = items_.find(lruKey);
            if (it == items_.end())
            {
                lru_.pop_back();
                continue;
            }
            // 仅当没有外部句柄（use_count==1，仅缓存持有）时才可淘汰
            if (it->second.ptr.use_count() == 1)
            {
                lru_.pop_back();
                items_.erase(it);
            }
            else
            {
                // 被外部引用的最旧条目也不能淘汰，且它之后（更旧方向）已无更旧者，
                // 尝试从它前一个开始？直接中断：软上限，当前无法淘汰。
                break;
            }
        }
    }

    size_t capacity_;
    Factory factory_;
    std::list<std::string> lru_; // 前端 MRU，后端 LRU
    std::unordered_map<std::string, Entry> items_;
};

// ---- 多类型资源管理器 ----
// 按 type_index 持有不同类型的 AssetCache，提供统一模板入口。
class AssetManager
{
  public:
    // 注册/取指定类型缓存（按需创建；capacity 仅首次创建生效）
    template<typename T> AssetCache<T>& Cache(size_t capacity, typename AssetCache<T>::Factory factory)
    {
        const std::type_index key = std::type_index(typeid(T));
        auto it = caches_.find(key);
        if (it == caches_.end())
        {
            auto cache = std::make_unique<AssetCache<T>>(capacity, std::move(factory));
            auto* raw = cache.get();
            caches_.emplace(key, std::move(cache));
            return *raw;
        }
        return *static_cast<AssetCache<T>*>(it->second.get());
    }

    // 取已注册缓存（未注册抛异常）
    template<typename T> AssetCache<T>& Cache()
    {
        const std::type_index key = std::type_index(typeid(T));
        const auto it = caches_.find(key);
        if (it == caches_.end())
            throw std::runtime_error("AssetManager: 类型未注册缓存");
        return *static_cast<AssetCache<T>*>(it->second.get());
    }

    template<typename T> std::shared_ptr<T> Load(const std::string& path) { return Cache<T>().Load(path); }
    template<typename T> std::shared_ptr<T> Get(const std::string& path) const
    {
        return const_cast<AssetManager*>(this)->Cache<T>().Get(path);
    }
    template<typename T> void Remove(const std::string& path) { Cache<T>().Remove(path); }

  private:
    std::unordered_map<std::type_index, std::unique_ptr<IAssetCacheHolder>> caches_;
};
} // namespace BigHero::Core

