#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// UiAnchors: normalized (0-1) anchor rectangle used to position a UI element
// relative to its parent. Holds anchor min/max plus pivot. Pure config.
class UiAnchors {
public:
    UiAnchors() {}
    UiAnchors(float minX, float minY, float maxX, float maxY)
        : minX_(minX), minY_(minY), maxX_(maxX), maxY_(maxY) {}

    void SetMin(float x, float y) { minX_=x; minY_=y; }
    void SetMax(float x, float y) { maxX_=x; maxY_=y; }
    void Min(float& x, float& y) const { x=minX_; y=minY_; }
    void Max(float& x, float& y) const { x=maxX_; y=maxY_; }
    void SetPivot(float x, float y) { px_=x; py_=y; }
    void Pivot(float& x, float& y) const { x=px_; y=py_; }
    void SetSizeDelta(float x, float y) { dx_=x; dy_=y; }
    void SizeDelta(float& x, float& y) const { x=dx_; y=dy_; }

    // Compute the world rect given parent size.
    void WorldRect(float parentW, float parentH,
                   float& x, float& y, float& w, float& h) const {
        w = (maxX_ - minX_) * parentW + dx_;
        h = (maxY_ - minY_) * parentH + dy_;
        x = minX_ * parentW - px_ * w;
        y = minY_ * parentH - py_ * h;
    }

    void SetStretch(bool s) { stretch_ = s; }
    bool IsStretch() const { return stretch_; }

private:
    float minX_=0, minY_=0, maxX_=0, maxY_=0;
    float px_=0.5f, py_=0.5f;
    float dx_=0, dy_=0;
    bool stretch_=false;
};

} // namespace bighero
