#pragma once
// 贝塞尔曲线（Bezier）：二次/三次贝塞尔曲线求值与采样。
// 纯标准库、仅头文件。
//
// 商业化价值：动画路径、UI 运动轨迹、编辑器曲线、粒子轨迹的几何基元；
// 提供位置与切线（速度）的解析计算。
//
// 约定：t 归一化 [0,1]。

namespace BigHero::Core
{
struct BezierVec2
{
    float x = 0, y = 0;
    BezierVec2() = default;
    BezierVec2(float x, float y) : x(x), y(y) {}
    BezierVec2 operator+(const BezierVec2& o) const { return { x + o.x, y + o.y }; }
    BezierVec2 operator-(const BezierVec2& o) const { return { x - o.x, y - o.y }; }
    BezierVec2 operator*(float s) const { return { x * s, y * s }; }
    BezierVec2& operator+=(const BezierVec2& o) { x += o.x; y += o.y; return *this; }
};

namespace Bezier
{
    // 二次贝塞尔：P0,P1,P2，t -> 点。
    inline BezierVec2 Quadratic(const BezierVec2& p0, const BezierVec2& p1,
                                const BezierVec2& p2, float t)
    {
        float mt = 1.0f - t;
        float a = mt * mt, b = 2.0f * mt * t, c = t * t;
        return p0 * a + p1 * b + p2 * c;
    }

    // 二次贝塞尔切线（一阶导，未归一化）。
    inline BezierVec2 QuadraticTangent(const BezierVec2& p0, const BezierVec2& p1,
                                       const BezierVec2& p2, float t)
    {
        return (p1 - p0) * (2.0f * (1.0f - t)) + (p2 - p1) * (2.0f * t);
    }

    // 三次贝塞尔：P0,P1,P2,P3。
    inline BezierVec2 Cubic(const BezierVec2& p0, const BezierVec2& p1,
                            const BezierVec2& p2, const BezierVec2& p3, float t)
    {
        float mt = 1.0f - t;
        float a = mt * mt * mt, b = 3.0f * mt * mt * t, c = 3.0f * mt * t * t, d = t * t * t;
        return p0 * a + p1 * b + p2 * c + p3 * d;
    }

    // 三次贝塞尔切线。
    inline BezierVec2 CubicTangent(const BezierVec2& p0, const BezierVec2& p1,
                                   const BezierVec2& p2, const BezierVec2& p3, float t)
    {
        float mt = 1.0f - t;
        return (p1 - p0) * (3.0f * mt * mt) + (p2 - p1) * (6.0f * mt * t) + (p3 - p2) * (3.0f * t * t);
    }
} // namespace Bezier
} // namespace BigHero::Core
