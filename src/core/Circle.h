#pragma once
// 圆（Circle）：2D 圆，用于碰撞检测与空间查询。
// 纯标准库、仅头文件。
//
// 商业化价值：移动体碰撞、拾取检测、最小包围的常用 2D 几何；点/圆相交 O(1)。

#include <cmath>

namespace BigHero::Core
{
struct Circle
{
    float cx = 0, cy = 0, r = 1.0f;

    Circle() = default;
    Circle(float cx, float cy, float r) : cx(cx), cy(cy), r(r) {}

    // 判断点是否在圆内（含边界）。
    bool ContainsPoint(float px, float py) const
    {
        float dx = px - cx, dy = py - cy;
        return (dx * dx + dy * dy) <= r * r;
    }

    // 判断两圆是否相交（含相切）。
    bool Overlaps(const Circle& o) const
    {
        float dx = cx - o.cx, dy = cy - o.cy;
        float dist2 = dx * dx + dy * dy;
        float rr = r + o.r;
        return dist2 <= rr * rr;
    }

    [[nodiscard]] float Area() const { return 3.14159265358979323846f * r * r; }
    [[nodiscard]] float Circumference() const { return 2.0f * 3.14159265358979323846f * r; }

    // 返回最近点到圆心距离的平方（用于快速粗判）。
    float DistanceSqToPoint(float px, float py) const
    {
        float dx = px - cx, dy = py - cy;
        return dx * dx + dy * dy;
    }

    // 缩放半径。
    void SetRadius(float nr) { r = nr > 0.0f ? nr : 0.0f; }
};
} // namespace BigHero::Core
