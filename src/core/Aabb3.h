#pragma once
// 3D 轴对齐包围盒（Aabb3）：用于碰撞检测与空间剔除。
// 纯标准库、仅头文件。
//
// 商业化价值：渲染视锥剔除、物理碰撞宽相位、空间分区的基础几何体；
// 提供 min/max 双角点表示与常用的包含/相交/合并操作。

#include <algorithm>

namespace BigHero::Core
{
struct Vec3
{
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
};

struct Aabb3
{
    Vec3 min;
    Vec3 max;

    Aabb3() = default;
    Aabb3(const Vec3& min, const Vec3& max) : min(min), max(max) {}

    // 从中心点与半尺寸构造。
    static Aabb3 FromCenterSize(const Vec3& center, const Vec3& size)
    {
        Vec3 half(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f);
        return Aabb3(center - half, center + half);
    }

    [[nodiscard]] Vec3 Center() const { return { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f }; }
    [[nodiscard]] Vec3 Extents() const { return { max.x - min.x, max.y - min.y, max.z - min.z }; }
    [[nodiscard]] float SizeX() const { return max.x - min.x; }
    [[nodiscard]] float SizeY() const { return max.y - min.y; }
    [[nodiscard]] float SizeZ() const { return max.z - min.z; }

    bool Contains(const Vec3& p) const
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    bool Overlaps(const Aabb3& o) const
    {
        return min.x <= o.max.x && o.min.x <= max.x &&
               min.y <= o.max.y && o.min.y <= max.y &&
               min.z <= o.max.z && o.min.z <= max.z;
    }

    // 合并到自身上。
    void ExpandToInclude(const Aabb3& o)
    {
        min.x = std::min(min.x, o.min.x); min.y = std::min(min.y, o.min.y); min.z = std::min(min.z, o.min.z);
        max.x = std::max(max.x, o.max.x); max.y = std::max(max.y, o.max.y); max.z = std::max(max.z, o.max.z);
    }
    void ExpandToInclude(const Vec3& p)
    {
        Aabb3 o(p, p);
        ExpandToInclude(o);
    }

    // 用给定扩展值外扩。
    void Inflate(float dx, float dy, float dz)
    {
        min.x -= dx; min.y -= dy; min.z -= dz;
        max.x += dx; max.y += dy; max.z += dz;
    }

    [[nodiscard]] bool IsDegenerate() const { return max.x < min.x || max.y < min.y || max.z < min.z; }
};
} // namespace BigHero::Core
