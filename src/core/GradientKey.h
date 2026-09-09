#pragma once
#include <cstddef>

namespace bighero {

// GradientKey: one color stop in a gradient at a normalized position (0..1).
// Pure data record used by gradient sampling.
class GradientKey {
public:
    GradientKey() {}
    GradientKey(float pos, float r, float g, float b, float a = 1.0f)
        : pos_(pos), r_(r), g_(g), b_(b), a_(a) {}

    void SetPosition(float p) { pos_ = p < 0 ? 0 : (p > 1 ? 1 : p); }
    float Position() const { return pos_; }
    void SetColor(float r, float g, float b, float a = 1.0f) { r_=r; g_=g; b_=b; a_=a; }
    void Color(float& r, float& g, float& b, float& a) const { r=r_; g=g_; b=b_; a=a_; }
    void SetMode(int m) { mode_ = m; }
    int Mode() const { return mode_; }

private:
    float pos_=0, r_=1, g_=1, b_=1, a_=1;
    int mode_ = 0;
};

} // namespace bighero
