#pragma once
// 轻量 ECS（EnTT 风格简化版）。纯 CPU、仅标准库，可离线单测。
//
// 设计：
//   - Entity：32 位打包句柄（低位 index 索引，高位 version 代号）。index 复用时代号递增，
//     防止悬垂句柄误用旧实体。
//   - Registry：实体生命周期（Create/Destroy/Alive）+ 稀疏集组件池（Add/Get/Has/Remove）+ View 迭代。
//   - SparseSet 组件池：dense(连续实体+组件)+sparse(实体index->dense位置)，O(1) 增删查，
//     迭代缓存友好；Remove 用 swap-pop 保持紧凑。
//   - View<T...>::Each(fn)：迭代同时拥有 T... 全部组件的实体，fn(T0&, T1&, ...) 提供各组件可变引用。

#include <algorithm>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BigHero::Core
{
// ---- 实体句柄 ----
class Entity
{
  public:
    Entity() = default;
    explicit Entity(uint32_t value) noexcept : value_(value) {}

    [[nodiscard]] uint32_t Index() const noexcept { return value_ & kIndexMask; }
    [[nodiscard]] uint32_t Version() const noexcept { return value_ >> kIndexBits; }
    [[nodiscard]] uint32_t Value() const noexcept { return value_; }
    [[nodiscard]] bool IsNull() const noexcept { return value_ == 0; }

    friend bool operator==(Entity a, Entity b) noexcept { return a.value_ == b.value_; }
    friend bool operator!=(Entity a, Entity b) noexcept { return a.value_ != b.value_; }

    static constexpr uint32_t kIndexBits = 20; // 支持 2^20 = 1048576 个并发实体
    static constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;

  private:
    uint32_t value_ = 0;
};

namespace detail
{
// 组件池基类：供 Registry 以 type-erased 方式统一移除/清空。
struct IComponentPool
{
    virtual ~IComponentPool() = default;
    virtual void RemoveEntity(uint32_t entityIndex) = 0;
    virtual void Clear() = 0;
};

// 稀疏集组件池。
template<typename T> class SparseSet : public IComponentPool
{
  public:
    struct Slot
    {
        Entity entity; // 完整句柄（含版本）
        T value;
    };

    T& Get(uint32_t entityIndex) noexcept { return dense_[sparse_[entityIndex]].value; }
    const T& Get(uint32_t entityIndex) const noexcept { return const_cast<SparseSet*>(this)->Get(entityIndex); }

    [[nodiscard]] bool Contains(uint32_t entityIndex) const noexcept
    {
        if (entityIndex >= sparse_.size())
            return false;
        const size_t pos = sparse_[entityIndex];
        return pos != kNone && pos < dense_.size() && dense_[pos].entity.Index() == entityIndex;
    }

    template<typename... Args> T& Emplace(Entity e, Args&&... args)
    {
        const uint32_t entityIndex = e.Index();
        if (Contains(entityIndex))
            return Get(entityIndex);
        const size_t pos = dense_.size();
        dense_.push_back(Slot{e, T(std::forward<Args>(args)...)});
        if (entityIndex >= sparse_.size())
            sparse_.resize(static_cast<size_t>(entityIndex) + 1, kNone);
        sparse_[entityIndex] = pos;
        return dense_[pos].value;
    }

    void RemoveEntity(uint32_t entityIndex) override
    {
        if (!Contains(entityIndex))
            return;
        const size_t pos = sparse_[entityIndex];
        const uint32_t lastEntity = dense_.back().entity.Index();
        dense_[pos] = std::move(dense_.back());
        dense_.pop_back();
        if (lastEntity != entityIndex)
            sparse_[lastEntity] = pos;
        sparse_[entityIndex] = kNone;
    }

    void Clear() override
    {
        dense_.clear();
        std::fill(sparse_.begin(), sparse_.end(), kNone);
    }

    [[nodiscard]] const std::vector<Slot>& Dense() const noexcept { return dense_; }
    [[nodiscard]] size_t Size() const noexcept { return dense_.size(); }

    // 预分配 dense 容量，减少批量添加组件时的重分配次数。
    void Reserve(size_t n) { dense_.reserve(n); }

  private:
    static constexpr size_t kNone = static_cast<size_t>(-1);
    std::vector<Slot> dense_;
    std::vector<size_t> sparse_;
};
} // namespace detail

// ---- 注册表 ----
class Registry
{
  public:
    Registry() = default;
    ~Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    // 创建实体：优先复用空闲 index（version 递增），否则新分配。
    // 保留 index 0 作为空实体哨兵（Entity() 默认值为 0），有效实体从 index 1 开始，
    // 使空实体与任何真实实体都不可混淆。
    [[nodiscard]] Entity Create()
    {
        if (!freeList_.empty())
        {
            const uint32_t idx = freeList_.back();
            freeList_.pop_back();
            entities_[idx].version += 1;
            entities_[idx].alive = true;
            return Entity(idx | (entities_[idx].version << Entity::kIndexBits));
        }
        const uint32_t idx = static_cast<uint32_t>(entities_.size());
        // 若尚未预留 index 0（空实体哨兵），先补一个占位记录，保证真实实体从 1 开始。
        if (idx == 0)
            entities_.push_back(EntityRecord{0, false});
        entities_.push_back(EntityRecord{0, true});
        return Entity(static_cast<uint32_t>(entities_.size()) - 1);
    }

    void Destroy(Entity e)
    {
        const uint32_t idx = e.Index();
        if (idx >= entities_.size() || !entities_[idx].alive)
            return;
        for (auto& [type, pool] : pools_)
            pool->RemoveEntity(idx);
        entities_[idx].alive = false;
        freeList_.push_back(idx);
    }

    [[nodiscard]] bool Alive(Entity e) const noexcept
    {
        const uint32_t idx = e.Index();
        if (idx >= entities_.size())
            return false;
        const EntityRecord& rec = entities_[idx];
        return rec.alive && rec.version == e.Version();
    }

    // 由 index 还原当前版本的真实实体（调试/测试用）
    [[nodiscard]] Entity MakeEntity(uint32_t index) const noexcept
    {
        if (index >= entities_.size())
            return Entity();
        return Entity(index | (entities_[index].version << Entity::kIndexBits));
    }

    // ---- 组件操作 ----
    template<typename T, typename... Args> T& Add(Entity e, Args&&... args)
    {
        return Pool<T>().Emplace(e, std::forward<Args>(args)...);
    }
    // 是否拥有组件 T。无副作用：未注册过该组件类型时返回 false（不创建空池）。
    template<typename T> bool Has(Entity e) const noexcept
    {
        const detail::SparseSet<T>* pool = FindPool<T>();
        return pool != nullptr && pool->Contains(e.Index());
    }
    template<typename T> T& Get(Entity e) noexcept { return Pool<T>().Get(e.Index()); }
    template<typename T> const T& Get(Entity e) const noexcept
    {
        return const_cast<Registry*>(this)->Pool<T>().Get(e.Index());
    }
    // 安全读取：实体无组件 T 时返回 nullptr（不抛异常、不建池），调用方判空后使用。
    template<typename T> T* TryGet(Entity e) noexcept
    {
        detail::SparseSet<T>* pool = FindPool<T>();
        return (pool != nullptr && pool->Contains(e.Index())) ? &pool->Get(e.Index()) : nullptr;
    }
    template<typename T> const T* TryGet(Entity e) const noexcept
    {
        const detail::SparseSet<T>* pool = FindPool<T>();
        return (pool != nullptr && pool->Contains(e.Index())) ? &pool->Get(e.Index()) : nullptr;
    }
    // 移除组件 T。无副作用：未注册过该组件类型时直接返回。
    template<typename T> void Remove(Entity e) noexcept
    {
        detail::SparseSet<T>* pool = FindPool<T>();
        if (pool != nullptr)
            pool->RemoveEntity(e.Index());
    }

    // 拥有组件 T 的实体数量（无副作用，未注册过该组件类型时返回 0）。
    template<typename T> size_t Count() const noexcept
    {
        const detail::SparseSet<T>* pool = FindPool<T>();
        return pool != nullptr ? pool->Size() : 0;
    }

    // 清空组件 T 的全部实例（实体本身保持存活）。
    template<typename T> void Clear() noexcept
    {
        detail::SparseSet<T>* pool = FindPool<T>();
        if (pool != nullptr)
            pool->Clear();
    }

    // 当前存活实体总数（总槽位 - 空闲槽位 - index0 永久哨兵）。
    // index 0 为 Create 首例时预留的空实体哨兵（永不参与复用），需在统计中扣除。
    [[nodiscard]] size_t EntityCount() const noexcept
    {
        const size_t sentinel = entities_.empty() ? 0 : 1;
        return entities_.size() - freeList_.size() - sentinel;
    }

    // 清空所有实体的全部组件（实体句柄与存活状态保持不变）。
    void ClearAllComponents() noexcept
    {
        for (auto& [type, pool] : pools_)
            pool->Clear();
    }

    // 组件池访问（供 View 使用）。公开以便 View 驱动迭代。
    template<typename T> detail::SparseSet<T>& Pool()
    {
        const std::type_index key = std::type_index(typeid(T));
        auto it = pools_.find(key);
        if (it != pools_.end())
            return *static_cast<detail::SparseSet<T>*>(it->second.get());
        auto ptr = std::make_unique<detail::SparseSet<T>>();
        auto* raw = ptr.get();
        pools_.emplace(key, std::move(ptr));
        return *raw;
    }

  private:
    // 仅查找已存在的组件池（不创建）。供 Has/TryGet/Remove/Count/Clear 等只读或防御性操作复用，
    // 避免对未注册组件类型的查询产生副作用（空池分配）。
    template<typename T> detail::SparseSet<T>* FindPool() noexcept
    {
        const auto it = pools_.find(std::type_index(typeid(T)));
        return it != pools_.end() ? static_cast<detail::SparseSet<T>*>(it->second.get()) : nullptr;
    }
    template<typename T> const detail::SparseSet<T>* FindPool() const noexcept
    {
        const auto it = pools_.find(std::type_index(typeid(T)));
        return it != pools_.end() ? static_cast<const detail::SparseSet<T>*>(it->second.get()) : nullptr;
    }

    struct EntityRecord
    {
        uint32_t version = 0;
        bool alive = false;
    };

    std::vector<EntityRecord> entities_;
    std::vector<uint32_t> freeList_;
    std::unordered_map<std::type_index, std::unique_ptr<detail::IComponentPool>> pools_;
};

// ---- View：迭代同时拥有 T... 全部组件的实体 ----
template<typename... Ts> class View
{
  public:
    explicit View(detail::SparseSet<Ts>&... pools) : pools_(pools...) {}

    // Each(fn)：fn(T0&, T1&, ...) 遍历同时拥有全部组件的实体，提供可变引用。
    // 以第一个组件池驱动，其余池做成员检查。
    template<typename F> void Each(F&& fn)
    {
        const auto& dense0 = std::get<0>(pools_).Dense();
        for (size_t i = 0; i < dense0.size(); ++i)
        {
            const uint32_t idx = dense0[i].entity.Index();
            if (!ContainsAll(idx))
                continue;
            Call(fn, idx, std::index_sequence_for<Ts...>{});
        }
    }

    [[nodiscard]] size_t Size() const noexcept { return std::get<0>(pools_).Dense().size(); }

  private:
    bool ContainsAll(const uint32_t idx) const { return ContainsAllImpl(idx, std::index_sequence_for<Ts...>{}); }
    template<size_t... Is> bool ContainsAllImpl(const uint32_t idx, std::index_sequence<Is...>) const
    {
        return (std::get<Is>(pools_).Contains(idx) && ...);
    }
    template<typename F, size_t... Is> void Call(F& fn, const uint32_t idx, std::index_sequence<Is...>)
    {
        fn(std::get<Is>(pools_).Get(idx)...);
    }

    std::tuple<detail::SparseSet<Ts>&...> pools_;
};

// 便捷：从注册表构造 View
template<typename... Ts> auto MakeView(Registry& reg)
{
    return View<Ts...>(reg.Pool<Ts>()...);
}
} // namespace BigHero::Core
