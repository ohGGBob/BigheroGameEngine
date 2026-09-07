#pragma once
// 类型安全变体（Variant）：可持有若干种类型之一，运行时按类型安全读取/赋值。
// 纯标准库、仅头文件。
//
// 商业化价值：事件负载、属性系统、脚本桥接、序列化中间值的基础类型；
// 比 C-style union 安全（自动构造/析构、类型标签、拷贝语义），比裸 void* 类型安全。
//
// 设计：类型擦除存储（Base + Holder<T>），std::type_index 记录当前类型；
//   - Emplace<T>(args...)：原地构造 T 并设为当前类型。
//   - Has<T>()：判断当前是否持有 T。
//   - Get<T>() / GetOr<T>(def) / TryGet<T>(out)：类型安全读取。
//   - Type() / TypeName()：查询当前持有的类型信息。
//   - 拷贝/移动均深拷贝，资源安全。

#include <memory>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <stdexcept>

namespace BigHero::Core
{
class Variant
{
  public:
    Variant() = default;
    Variant(const Variant& o) { CopyFrom(o); }
    Variant& operator=(const Variant& o)
    {
        if (this != &o) { Reset(); CopyFrom(o); }
        return *this;
    }
    Variant(Variant&& o) noexcept { MoveFrom(o); }
    Variant& operator=(Variant&& o) noexcept
    {
        if (this != &o) { Reset(); MoveFrom(o); }
        return *this;
    }
    ~Variant() = default;

    template<typename T, typename... Args>
    T& Emplace(Args&&... args)
    {
        Reset();
        auto holder = std::make_unique<Holder<T>>(std::forward<Args>(args)...);
        type_ = std::type_index(typeid(T));
        T& ref = holder->value;
        storage_ = std::move(holder);
        return ref;
    }

    template<typename T> bool Has() const { return type_ == std::type_index(typeid(T)); }
    [[nodiscard]] bool Empty() const { return !storage_; }
    [[nodiscard]] std::type_index Type() const { return type_; }
    // 返回当前持有类型的 RTTI 名称（如 "int"）；无法直接取 type_info 引用，
    // 因为 std::type_index 只提供 name()。需要完整 type_info 时用 typeid(T) 推断。
    [[nodiscard]] const char* TypeName() const { return type_.name(); }

    void Reset()
    {
        storage_.reset();
        type_ = std::type_index(typeid(void));
    }

    template<typename T> const T& Get() const
    {
        if (!Has<T>())
            throw std::bad_cast();
        return static_cast<const Holder<T>*>(storage_.get())->value;
    }
    template<typename T> T& Get()
    {
        if (!Has<T>())
            throw std::bad_cast();
        return static_cast<Holder<T>*>(storage_.get())->value;
    }
    template<typename T> const T& GetOr(const T& def) const { return Has<T>() ? Get<T>() : def; }
    template<typename T> bool TryGet(T& out) const
    {
        if (!Has<T>())
            return false;
        out = Get<T>();
        return true;
    }

  private:
    struct Base
    {
        virtual ~Base() = default;
        virtual std::unique_ptr<Base> Clone() const = 0;
    };
    template<typename T>
    struct Holder : Base
    {
        template<typename... Args> explicit Holder(Args&&... args) : value(std::forward<Args>(args)...) {}
        std::unique_ptr<Base> Clone() const override { return std::make_unique<Holder<T>>(value); }
        T value;
    };

    void CopyFrom(const Variant& o)
    {
        if (!o.storage_) { Reset(); return; }
        type_ = o.type_;
        storage_ = o.storage_->Clone();
    }
    void MoveFrom(Variant& o)
    {
        if (!o.storage_) { Reset(); return; }
        type_ = o.type_;
        storage_ = std::move(o.storage_);
        o.type_ = std::type_index(typeid(void));
    }

    std::unique_ptr<Base> storage_;
    std::type_index type_{ typeid(void) };
};
} // namespace BigHero::Core
