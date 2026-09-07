#pragma once
// 位标志容器（BitFlags）：紧凑的二进制标志位组合管理。
// 纯标准库、仅头文件。
//
// 商业化价值：多开关状态（系统启用位、特性开关、渲染通道开关、实体标签）的紧凑表示，
// 比多个 bool 更省内存、更易序列化/拷贝，支持位运算型 API。
//
// 提供：Set/Unset/Test/Toggle/HasAny/HasAll/Clear/Count；模板化枚举类型（enum class 友好）。

#include <cstdint>
#include <type_traits>

namespace BigHero::Core
{
template<typename T> class BitFlags
{
    static_assert(std::is_integral_v<T>, "BitFlags requires an integral flag type");
  public:
    using Underlying = T;

    constexpr BitFlags() noexcept = default;
    constexpr BitFlags(T flags) noexcept : flags_(flags) {}

    constexpr void Set(T flag) noexcept { flags_ = (flags_ | flag); }
    constexpr void Unset(T flag) noexcept { flags_ = (flags_ & ~flag); }
    constexpr void Toggle(T flag) noexcept { flags_ = (flags_ ^ flag); }
    constexpr void Clear() noexcept { flags_ = {}; }

    [[nodiscard]] constexpr bool Test(T flag) const noexcept { return (flags_ & flag) != T{}; }
    [[nodiscard]] constexpr bool HasAny(T flag) const noexcept { return (flags_ & flag) != T{}; }
    [[nodiscard]] constexpr bool HasAll(T flag) const noexcept { return ((flags_ & flag) == flag); }
    [[nodiscard]] constexpr bool None() const noexcept { return flags_ == T{}; }
    [[nodiscard]] constexpr bool Any() const noexcept { return flags_ != T{}; }

    [[nodiscard]] constexpr T Get() const noexcept { return flags_; }
    constexpr void SetValue(T v) noexcept { flags_ = v; }

    [[nodiscard]] constexpr int Count() const noexcept
    {
        // popcount（通用，不限平台）
        using U = std::make_unsigned_t<T>;
        U x = static_cast<U>(flags_);
        int c = 0;
        while (x) { x &= (x - 1); ++c; }
        return c;
    }

    constexpr BitFlags& operator|=(T flag) noexcept { Set(flag); return *this; }
    constexpr BitFlags& operator&=(T flag) noexcept { flags_ &= flag; return *this; }
    constexpr BitFlags& operator^=(T flag) noexcept { Toggle(flag); return *this; }

    [[nodiscard]] constexpr BitFlags operator|(T flag) const noexcept { return BitFlags(flags_ | flag); }
    [[nodiscard]] constexpr BitFlags operator&(T flag) const noexcept { return BitFlags(flags_ & flag); }
    [[nodiscard]] constexpr BitFlags operator^(T flag) const noexcept { return BitFlags(flags_ ^ flag); }
    [[nodiscard]] constexpr BitFlags operator~() const noexcept { return BitFlags(~flags_); }

    [[nodiscard]] constexpr bool operator==(const BitFlags& o) const noexcept { return flags_ == o.flags_; }
    [[nodiscard]] constexpr bool operator!=(const BitFlags& o) const noexcept { return flags_ != o.flags_; }

  private:
    T flags_{};
};
} // namespace BigHero::Core
