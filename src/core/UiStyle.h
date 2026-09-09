#pragma once
#include <cstddef>
#include <cstdint>

namespace bighero {

// UiStyle: per-widget style overrides — background color, border radius,
// border thickness, and a fill/pressed color. Pure config container for the
// UI renderer. Standard-library only.
class UiStyle {
public:
    UiStyle() {}

    void SetBackground(float r, float g, float b, float a = 1.0f) {
        bgR_=r; bgG_=g; bgB_=b; bgA_=a;
    }
    void Background(float& r, float& g, float& b, float& a) const {
        r=bgR_; g=bgG_; b=bgB_; a=bgA_;
    }
    void SetBorderColor(float r, float g, float b, float a = 1.0f) {
        br_=r; bgG_=g; bgB_=b; bgA_=a;  // reuse fields with border setter
        br_=r; bgr_=g; bgb_=b; bga_=a;
    }
    void BorderColor(float& r, float& g, float& b, float& a) const {
        r=br_; g=bgr_; b=bgb_; a=bga_;
    }
    void SetBorderRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float BorderRadius() const { return radius_; }
    void SetBorderThickness(float t) { border_ = t < 0 ? 0 : t; }
    float BorderThickness() const { return border_; }
    void SetAlign(int a) { align_ = a; }
    int Align() const { return align_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    float bgR_=0, bgG_=0, bgB_=0, bgA_=0;      // background
    float br_=0, bgr_=0, bgb_=0, bga_=1;       // border color
    float radius_=0, border_=0;
    int align_ = 0;
    bool enabled_ = true;
};

} // namespace bighero
