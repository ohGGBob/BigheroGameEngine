#pragma once
// 矩形（Rect）：2D 矩形（xy 原点 + 宽高），提供命中测试与常用几何操作。
// 纯标准库、仅头文件。
//
// 商业化价值：UI 布局、相机裁剪、碰撞 AABB、纹理区域切分的统一表示。

#include <algorithm>

namespace BigHero::Core
{
struct Rect
{
    float x = 0, y = 0, w = 0, h = 0;

    constexpr Rect() = default;
    constexpr Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}

    // 由中心点+宽高构造。
    static Rect FromCenter(const float cx, const float cy, const float width, const float height)
    {
        return Rect(cx - width * 0.5f, cy - height * 0.5f, width, height);
    }

    [[nodiscard]] float Left() const { return x; }
    [[nodiscard]] float Top() const { return y; }
    [[nodiscard]] float Right() const { return x + w; }
    [[nodiscard]] float Bottom() const { return y + h; }
    [[nodiscard]] float Width() const { return w; }
    [[nodiscard]] float Height() const { return h; }
    [[nodiscard]] float CenterX() const { return x + w * 0.5f; }
    [[nodiscard]] float CenterY() const { return y + h * 0.5f; }
    [[nodiscard]] float Area() const { return w * h; }
    [[nodiscard]] bool Empty() const { return w <= 0.0f || h <= 0.0f; }

    [[nodiscard]] bool Contains(float px, float py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    [[nodiscard]] bool Overlaps(const Rect& o) const
    {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }

    // 交集（不重叠返回空矩形）。
    Rect Intersect(const Rect& o) const
    {
        float nx = std::max(x, o.x);
        float ny = std::max(y, o.y);
        float nr = std::min(x + w, o.x + o.w);
        float nb = std::min(y + h, o.y + o.h);
        if (nr < nx || nb < ny)
            return Rect(0, 0, 0, 0);
        return Rect(nx, ny, nr - nx, nb - ny);
    }

    void Inflate(float dx, float dy)
    {
        x -= dx;
        y -= dy;
        w += dx * 2.0f;
        h += dy * 2.0f;
    }

    void MoveTo(float nx, float ny)
    {
        x = nx;
        y = ny;
    }

    void Set(float nx, float ny, float nw, float nh)
    {
        x = nx;
        y = ny;
        w = nw;
        h = nh;
    }

    bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};
} // namespace BigHero::Core
