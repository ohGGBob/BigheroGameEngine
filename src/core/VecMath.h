#pragma once
// 向量数学工具（VecMath）：常见 2D/3D 向量运算。
// 纯标准库、仅头文件。
//
// 商业化价值：为引擎数学提供轻量基础向量运算，避免处处手写；
// 与 Vec2/Vec3 及引擎内 glm 类型互补，可独立使用。

#include <cmath>

namespace BigHero::Core
{
namespace VecMath
{
    struct Vec2f { float x, y; };
    struct Vec3f { float x, y, z; };

    // 点积 / 叉积 / 长度 / 归一化 / 距离
    inline float Dot2(float ax, float ay, float bx, float by)
    {
        return ax * bx + ay * by;
    }
    inline float Cross2(float ax, float ay, float bx, float by)
    {
        return ax * by - ay * bx;
    }
    inline float Length2(float x, float y)
    {
        return std::sqrt(x * x + y * y);
    }
    inline float Dist2(float ax, float ay, float bx, float by)
    {
        float dx = ax - bx, dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    }

    inline float Dot3(float ax, float ay, float az, float bx, float by, float bz)
    {
        return ax * bx + ay * by + az * bz;
    }
    inline float Length3(float x, float y, float z)
    {
        return std::sqrt(x * x + y * y + z * z);
    }
    inline float Dist3(float ax, float ay, float az, float bx, float by, float bz)
    {
        float dx = ax - bx, dy = ay - by, dz = az - bz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // 夹取到 [lo, hi]
    inline float Clamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // 线性插值
    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    // 平滑阶跃（Hermite）
    inline float Smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

    // 判断点是否在三角形内（2D，含边界）。
    inline bool PointInTriangle2D(float px, float py,
                                  float ax, float ay, float bx, float by, float cx, float cy)
    {
        float d1 = Cross2(px - ax, py - ay, bx - ax, by - ay);
        float d2 = Cross2(px - bx, py - by, cx - bx, cy - by);
        float d3 = Cross2(px - cx, py - cy, ax - cx, ay - cy);
        bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(hasNeg && hasPos);
    }
} // namespace VecMath
} // namespace BigHero::Core
