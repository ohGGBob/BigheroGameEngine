#pragma once
// 类型特性工具（TypeTraits）：常用类型检测/转换的便捷别名。
// 纯标准库、仅头文件。
//
// 商业化价值：模板元编程语义的统一入口，避免各处手写 std:: 判定；
// 为引擎内部泛型代码（序列化、反射、容器、数学）提供一致的特性集。

#include <type_traits>
#include <memory>
#include <utility>

namespace BigHero::Core
{
// 依赖注入：检测类型是否含 begin()/end()（判断是否可范围迭代）
template<typename T, typename = void> struct IsRange : std::false_type {};
template<typename T>
struct IsRange<T, std::void_t<decltype(std::declval<T&>().begin()), decltype(std::declval<T&>().end())>>
    : std::true_type {};

template<typename T> inline constexpr bool IsRangeV = IsRange<T>::value;

// 是否可拷贝构造/赋值
template<typename T> inline constexpr bool IsCopyable =
    std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>;
// 是否可移动构造/赋值
template<typename T> inline constexpr bool IsMovable =
    std::is_move_constructible_v<T> && std::is_move_assignable_v<T>;
// 是否默认可构造
template<typename T> inline constexpr bool IsDefaultConstructible =
    std::is_default_constructible_v<T>;
// 是否算术类型且非 bool
template<typename T> inline constexpr bool IsArithmetic =
    std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
// 是否指针
template<typename T> inline constexpr bool IsPointer = std::is_pointer_v<std::decay_t<T>>;
// 去除引用与 cv 后的裸类型别名
template<typename T> using Clean = std::remove_cv_t<std::remove_reference_t<T>>;
// std::make_shared 便捷别名
template<typename T, typename... Args> std::shared_ptr<T> MakeShared(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}
// std::make_unique 便捷别名
template<typename T, typename... Args> std::unique_ptr<T> MakeUnique(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}
// 检测类型是否可哈希（供 unordered 容器 trait）
template<typename T, typename = void> struct IsHashable : std::false_type {};
template<typename T>
struct IsHashable<T, std::void_t<decltype(std::declval<std::hash<T>>()(std::declval<T>()))>>
    : std::true_type {};
template<typename T> inline constexpr bool IsHashableV = IsHashable<T>::value;
} // namespace BigHero::Core
