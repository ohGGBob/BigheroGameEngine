#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// LayoutRect: a computed rectangle in a layout tree with alignment, padding,
// margin, and size. Pure CPU-side layout descriptor + helpers.
class LayoutRect {
public:
    LayoutRect() {}
    LayoutRect(float x, float y, float w, float h)
        : x_(x), y_(y), w_(w), h_(h) {}

    void SetRect(float x, float y, float w, float h) { x_=x; y_=y; w_=w; h_=h; }
    void Position(float& x, float& y) const { x=x_; y=y_; }
    void Size(float& w, float& h) const { w=w_; h=h_; }
    float X() const { return x_; }
    float Y() const { return y_; }
    float Width() const { return w_; }
    float Height() const { return h_; }
    float Right() const { return x_ + w_; }
    float Bottom() const { return y_ + h_; }

    void SetPadding(float l, float t, float r, float b) { pl_=l; pt_=t; pr_=r; pb_=b; }
    void Padding(float& l, float& t, float& r, float& b) const { l=pl_; t=pt_; r=pr_; b=pb_; }
    void SetMargin(float l, float t, float r, float b) { ml_=l; mt_=t; mr_=r; mb_=b; }
    void Margin(float& l, float& t, float& r, float& b) const { l=ml_; t=mt_; r=mr_; b=mb_; }

    // Content rect after subtracting padding.
    void ContentRect(float& x, float& y, float& w, float& h) const {
        x = x_ + pl_; y = y_ + pt_; w = w_ - pl_ - pr_; h = h_ - pt_ - pb_;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
    }

    bool Contains(float px, float py) const {
        return px >= x_ && px <= Right() && py >= y_ && py <= Bottom();
    }
    bool Overlaps(const LayoutRect& o) const {
        return !(Right() < o.x_ || o.Right() < x_ || Bottom() < o.y_ || o.Bottom() < y_);
    }

private:
    float x_=0, y_=0, w_=0, h_=0;
    float pl_=0, pt_=0, pr_=0, pb_=0;
    float ml_=0, mt_=0, mr_=0, mb_=0;
};

} // namespace bighero
