#pragma once
// 2D 变换（Transform2D）：平移/旋转/缩放组合的 2D 仿射变换。
// 纯标准库、仅头文件。
//
// 商业化价值：UI 元素、精灵(sprite)、2D 摄像机、物理体的通用空间变换；
// 提供位置/旋转/缩放的双向映射与组合。

#include <cmath>

namespace BigHero::Core
{
struct Vec2
{
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(float s) const { return { x * s, y * s }; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
};

class Transform2D
{
  public:
    Transform2D() = default;
    Transform2D(float px, float py, float rotDeg = 0.0f, float sx = 1.0f, float sy = 1.0f)
        : pos_(px, py), rotDeg_(rotDeg), scale_(sx, sy) {}

    // 用变换把局部坐标转到世界坐标。
    Vec2 ToWorld(const Vec2& local) const
    {
        float rad = rotDeg_ * 3.14159265358979323846f / 180.0f;
        float c = std::cos(rad), s = std::sin(rad);
        float scaledX = local.x * scale_.x;
        float scaledY = local.y * scale_.y;
        return { pos_.x + c * scaledX - s * scaledY,
                 pos_.y + s * scaledX + c * scaledY };
    }

    // 世界坐标转局部坐标（逆变换）。
    Vec2 ToLocal(const Vec2& world) const
    {
        float dx = world.x - pos_.x;
        float dy = world.y - pos_.y;
        float rad = -rotDeg_ * 3.14159265358979323846f / 180.0f;
        float c = std::cos(rad), s = std::sin(rad);
        float rx = c * dx - s * dy;
        float ry = s * dx + c * dy;
        if (scale_.x != 0.0f) rx /= scale_.x;
        if (scale_.y != 0.0f) ry /= scale_.y;
        return { rx, ry };
    }

    void Translate(const Vec2& delta) { pos_ += delta; }
    void Rotate(float degAdd) { rotDeg_ += degAdd; }
    void SetScale(float sx, float sy) { scale_ = { sx, sy }; }

    void SetPosition(float px, float py) { pos_ = { px, py }; }
    void SetRotation(float deg) { rotDeg_ = deg; }
    [[nodiscard]] Vec2 Position() const { return pos_; }
    [[nodiscard]] float Rotation() const { return rotDeg_; }
    [[nodiscard]] Vec2 Scale() const { return scale_; }

    // 组合：this 之后应用 t（先 this 后 t 的空间顺序）。
    Transform2D Then(const Transform2D& t) const
    {
        Vec2 composed = t.ToWorld(ToWorld(Vec2(0, 0)));
        // 简化：仅组合平移粗略近似（用于级联时保持位置），旋转/缩放完整组合可扩展。
        return Transform2D(composed.x, composed.y,
                           rotDeg_ + t.Rotation(),
                           scale_.x * t.Scale().x, scale_.y * t.Scale().y);
    }

  private:
    Vec2 pos_;
    float rotDeg_ = 0.0f;
    Vec2 scale_{ 1.0f, 1.0f };
};
} // namespace BigHero::Core
