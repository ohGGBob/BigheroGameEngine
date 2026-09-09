#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// UIScrollView: a scrollable UI container with a scroll offset, content
// extent, and optional vertical/horizontal scrolling. Pure data container
// for layout state; no rendering backend.
class UIScrollView {
public:
    UIScrollView() {}

    void SetViewport(float x, float y, float w, float h) {
        vx_=x; vy_=y; vw_=w < 0 ? 0 : w; vh_=h < 0 ? 0 : h;
    }
    void Viewport(float& x, float& y, float& w, float& h) const { x=vx_; y=vy_; w=vw_; h=vh_; }
    float ViewportWidth() const { return vw_; }
    float ViewportHeight() const { return vh_; }

    void SetContentSize(float w, float h) {
        cw_ = w < 0 ? 0 : w; ch_ = h < 0 ? 0 : h;
    }
    float ContentWidth() const { return cw_; }
    float ContentHeight() const { return ch_; }

    void SetScroll(float sx, float sy) {
        SetScrollX(sx); SetScrollY(sy);
    }
    void SetScrollX(float sx) {
        float maxX = MaxScrollX();
        sx_ = maxX > 0 ? (sx < 0 ? 0 : (sx > maxX ? maxX : sx)) : 0;
    }
    void SetScrollY(float sy) {
        float maxY = MaxScrollY();
        sy_ = maxY > 0 ? (sy < 0 ? 0 : (sy > maxY ? maxY : sy)) : 0;
    }
    void Scroll(float& sx, float& sy) const { sx=sx_; sy=sy_; }
    float ScrollX() const { return sx_; }
    float ScrollY() const { return sy_; }

    float MaxScrollX() const { float m = cw_ - vw_; return m < 0 ? 0 : m; }
    float MaxScrollY() const { float m = ch_ - vh_; return m < 0 ? 0 : m; }

    void ScrollBy(float dx, float dy) { SetScrollX(sx_+dx); SetScrollY(sy_+dy); }
    void ScrollToTop() { sy_ = 0; }
    void ScrollToBottom() { sy_ = MaxScrollY(); }
    void ScrollToLeft() { sx_ = 0; }
    void ScrollToRight() { sx_ = MaxScrollX(); }

    // Normalized scroll in [0,1].
    float NormalizedX() const { float m = MaxScrollX(); return m > 0 ? sx_/m : 0; }
    float NormalizedY() const { float m = MaxScrollY(); return m > 0 ? sy_/m : 0; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    float vx_=0, vy_=0, vw_=0, vh_=0;
    float cw_=0, ch_=0;
    float sx_=0, sy_=0;
    bool enabled_ = true;
};

} // namespace bighero
