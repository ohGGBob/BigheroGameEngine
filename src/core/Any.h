#pragma once
// 类型擦除容器（Any）：可持有任意单个值，运行时按类型安全取出。
// 纯标准库、仅头文件。
//
// 商业化价值：通用参数装箱、插件/回调签名解耦、属性包、跨模块传值的泛用容器。
//
// 设计：类似 std::any 但足够轻量（无 SBO），Deep copy；提供 Has<T>/Get<T>/TryGet<T>。

#include <memory>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <stdexcept>

namespace BigHero::Core
{
class Any
{
  public:
    Any() = default;
    template<typename T> explicit Any(T&& v) { Set(std::forward<T>(v)); }
    Any(const Any& o) { CopyFrom(o); }
    Any& operator=(const Any& o) { if (this != &o) { Reset(); CopyFrom(o); } return *this; }
    Any(Any&& o) noexcept { MoveFrom(o); }
    Any& operator=(Any&& o) noexcept { if (this != &o) { Reset(); MoveFrom(o); } return *this; }
    ~Any() = default;

    template<typename T> Any& Set(T&& v)
    {
        Reset();
        using D = std::decay_t<T>;
        storage_ = std::make_unique<Holder<D>>(std::forward<T>(v));
        type_ = std::type_index(typeid(D));
        return *this;
    }

    template<typename T> bool Has() const { return type_ == std::type_index(typeid(std::decay_t<T>)); }
    [[nodiscard]] bool Empty() const { return !storage_; }
    [[nodiscard]] std::type_index Type() const { return type_; }
    [[nodiscard]] const char* TypeName() const { return type_.name(); }

    void Reset() { storage_.reset(); type_ = std::type_index(typeid(void)); }

    template<typename T> const std::decay_t<T>& Get() const
    {
        if (!Has<T>())
            throw std::bad_cast();
        return static_cast<const Holder<std::decay_t<T>>*>(storage_.get())->value;
    }
    template<typename T> std::decay_t<T>& Get()
    {
        if (!Has<T>())
            throw std::bad_cast();
        return static_cast<Holder<std::decay_t<T>>*>(storage_.get())->value;
    }
    template<typename T> bool TryGet(std::decay_t<T>& out) const
    {
        if (!Has<T>())
            return false;
        out = Get<T>();
        return true;
    }

  private:
    struct Base { virtual ~Base() = default; virtual std::unique_ptr<Base> Clone() const = 0; };
    template<typename T> struct Holder : Base
    {
        template<typename... Args> explicit Holder(Args&&... args) : value(std::forward<Args>(args)...) {}
        std::unique_ptr<Base> Clone() const override { return std::make_unique<Holder<T>>(value); }
        T value;
    };
    void CopyFrom(const Any& o) { if (!o.storage_) { Reset(); return; } type_ = o.type_; storage_ = o.storage_->Clone(); }
    void MoveFrom(Any& o) { if (!o.storage_) { Reset(); return; } type_ = o.type_; storage_ = std::move(o.storage_); o.type_ = std::type_index(typeid(void)); }

    std::unique_ptr<Base> storage_;
    std::type_index type_{ typeid(void) };
};
} // namespace BigHero::Core
