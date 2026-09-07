#pragma once

namespace bighero {

// 2D orthographic camera used for world<->screen transforms.
struct Camera2D {
    float x, y;      // world position of camera center
    float zoom;      // scale factor (pixels per world unit)
    int viewW, viewH; // viewport size in pixels

    Camera2D() : x(0), y(0), zoom(1.0f), viewW(1280), viewH(720) {}

    void SetViewport(int w, int h) { viewW = w; viewH = h; }

    void Move(float dx, float dy) { x += dx; y += dy; }

    // World -> screen (pixel) coordinates.
    void WorldToScreen(float wx, float wy, float& sx, float& sy) const {
        sx = (wx - x) * zoom + viewW * 0.5f;
        sy = (y - wy) * zoom + viewH * 0.5f;
    }

    // Screen (pixel) -> world coordinates.
    void ScreenToWorld(float sx, float sy, float& wx, float& wy) const {
        wx = (sx - viewW * 0.5f) / zoom + x;
        wy = y - (sy - viewH * 0.5f) / zoom;
    }
};

} // namespace bighero
