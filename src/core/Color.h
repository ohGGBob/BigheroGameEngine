#pragma once
// 颜色容器（Color）：RGBA 颜色，支持整数/浮点双视图与常用转换。
// 纯标准库、仅头文件。
//
// 商业化价值：渲染材质、UI 主题、动画过渡的统一颜色表示；
// 提供 float 通道（0..1）与自动转 8 位整数的辅助。

#include <cmath>
#include <cstdint>

namespace BigHero::Core
{
struct Color
{
    float r = 0, g = 0, b = 0, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    // 从 8 位无符号（0..255）构造（自动归一化到 0..1）。
    static Color FromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }
    // 从 16 位 HEX 值构造，如 0xFF336699（AARRGGBB）。
    static Color FromHex(uint32_t hex)
    {
        return FromRGBA8((uint8_t)(hex & 0xFF), (uint8_t)((hex >> 8) & 0xFF),
                         (uint8_t)((hex >> 16) & 0xFF), (uint8_t)((hex >> 24) & 0xFF));
    }

    // 转 8 位无符号（自动夹取 0..255）。
    uint8_t R8() const { return To8(r); }
    uint8_t G8() const { return To8(g); }
    uint8_t B8() const { return To8(b); }
    uint8_t A8() const { return To8(a); }

    // 转 32 位 HEX（AARRGGBB）。
    uint32_t ToHex() const
    {
        return ((uint32_t)A8() << 24) | ((uint32_t)R8() << 16) | ((uint32_t)G8() << 8) | B8();
    }

    Color Clamped() const { return Color(Clamp01(r), Clamp01(g), Clamp01(b), Clamp01(a)); }

    Color operator+(const Color& o) const { return { r + o.r, g + o.g, b + o.b, a + o.a }; }
    Color operator*(float s) const { return { r * s, g * s, b * s, a * s }; }
    // 线性插值。
    Color Lerp(const Color& o, float t) const
    {
        return { r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t };
    }
    // alpha 混合（this 为背景，o 为前景）。
    Color Blend(const Color& fg) const
    {
        float na = fg.a + a * (1.0f - fg.a);
        if (na <= 1e-6f)
            return *this;
        float nr = (fg.r * fg.a + r * a * (1.0f - fg.a)) / na;
        float ng = (fg.g * fg.a + g * a * (1.0f - fg.a)) / na;
        float nb = (fg.b * fg.a + b * a * (1.0f - fg.a)) / na;
        return { nr, ng, nb, na };
    }

    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }

  private:
    static uint8_t To8(float v)
    {
        v = Clamp01(v);
        return (uint8_t)std::lround(v * 255.0f);
    }
    static float Clamp01(float v)
    {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }
};
} // namespace BigHero::Core
