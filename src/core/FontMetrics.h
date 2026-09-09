#pragma once
#include <cstddef>

namespace bighero {

// FontMetrics: per-font metrics — ascent, descent, line-gap, and a scale
// factor. Pure descriptor used to lay out glyphs in a text run.
class FontMetrics {
public:
    FontMetrics() {}
    FontMetrics(float ascent, float descent, float lineGap)
        : ascent_(ascent), descent_(descent), lineGap_(lineGap) {}

    void SetAscent(float a) { ascent_ = a; }
    float Ascent() const { return ascent_; }
    void SetDescent(float d) { descent_ = d; }
    float Descent() const { return descent_; }
    void SetLineGap(float g) { lineGap_ = g; }
    float LineGap() const { return lineGap_; }
    void SetScale(float s) { scale_ = s; }
    float Scale() const { return scale_; }
    void SetUnitsPerEm(float u) { unitsPerEm_ = u < 1 ? 1 : u; }
    float UnitsPerEm() const { return unitsPerEm_; }

    // Height of a single line in em units.
    float LineHeight() const { return ascent_ + descent_ + lineGap_; }
    // Compute a pixel scale factor from em to pixels for a given pixel size.
    float EmToPixel(float pixelSize) const {
        return pixelSize / unitsPerEm_;
    }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    float ascent_ = 0.8f, descent_ = 0.2f, lineGap_ = 0.0f;
    float scale_ = 1.0f, unitsPerEm_ = 1000.0f;
    bool enabled_ = true;
};

} // namespace bighero
