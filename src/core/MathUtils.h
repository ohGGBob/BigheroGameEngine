#pragma once
// 通用数学工具集（MathUtils）：纯标准库、仅头文件。（不含 glm，可离线单测）
//
// 提供标量/基础数学辅助：Clamp / Lerp / SmoothStep / Abs / Min / Max / Sign /
// IsPowerOfTwo / NextPow2 / WrappedAngleDeg / SafeNormalize 等。
// 面向"无 glm 依赖的纯逻辑代码"（如玩法、UI、编辑器逻辑）。

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace BigHero::Core
{
namespace math
{
// 夹取 v 到 [lo, hi]。
template<typename T> constexpr T Clamp(T v, T lo, T hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}
template<typename T> constexpr T Lerp(T a, T b, float t) noexcept
{
    return static_cast<T>(a + (b - a) * t);
}
template<typename T> constexpr T Abs(T v) noexcept
{
    return v < T(0) ? -v : v;
}
template<typename T> constexpr T Min(T a, T b) noexcept { return a < b ? a : b; }
template<typename T> constexpr T Max(T a, T b) noexcept { return a > b ? a : b; }

// 符号函数：返回 -1/0/1。
template<typename T> constexpr int Sign(T v) noexcept { return (v > T(0)) - (v < T(0)); }

// 光滑插值（避免线性插值的生硬拐点）。
template<typename T> constexpr T SmoothStep(T edge0, T edge1, T x) noexcept
{
    const T t = Clamp<T>((x - edge0) / (edge1 - edge0), T(0), T(1));
    return t * t * (T(3) - T(2) * t);
}

inline constexpr bool IsPowerOfTwo(uint64_t v) noexcept { return v != 0 && (v & (v - 1)) == 0; }

// 向上取整到不小于 v 的 2 的幂。
inline uint64_t NextPow2(uint64_t v) noexcept
{
    if (v == 0)
        return 1;
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v |= v >> 32;
    return v + 1;
}

// 把角度规整到 [0, 360)。
inline double WrapDeg(double deg) noexcept
{
    double r = std::fmod(deg, 360.0);
    if (r < 0)
        r += 360.0;
    return r;
}
// 把角度规整到 [-180, 180)。
inline double WrapDegSigned(double deg) noexcept
{
    double r = std::fmod(deg + 180.0, 360.0);
    if (r < 0)
        r += 360.0;
    return r - 180.0;
}

inline constexpr float DegreesToRadians(float deg) noexcept { return deg * 0.01745329252f; }
inline constexpr float RadiansToDegrees(float rad) noexcept { return rad * 57.295779513f; }

// 安全归一化的角度差（返回 [-180, 180) 的最短差值）。
inline double AngleDiff(double a, double b) noexcept
{
    return WrapDegSigned(b - a);
}
} // namespace math
} // namespace BigHero::Core
