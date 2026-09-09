#pragma once

namespace bighero {

// ScreenSpaceOverlay: describes a full-screen UI/pass overlay that draws on
// top of the scene. Tracks enabled state, blend mode, and distortion/vignette
// intensity hints. Pure data container.
class ScreenSpaceOverlay {
public:
    enum class Blend { Opaque, Alpha, Additive, Multiply };

    ScreenSpaceOverlay() {}
    explicit ScreenSpaceOverlay(bool enabled) : enabled_(enabled) {}

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void Toggle() { enabled_ = !enabled_; }

    void SetBlend(Blend b) { blend_ = b; }
    Blend BlendMode() const { return blend_; }

    void SetOpacity(float o) { opacity_ = o < 0 ? 0 : (o > 1 ? 1 : o); }
    float Opacity() const { return opacity_; }

    void SetVignette(float v) { vignette_ = v < 0 ? 0 : (v > 1 ? 1 : v); }
    float Vignette() const { return vignette_; }
    void SetDistortion(float d) { distortion_ = d; }
    float Distortion() const { return distortion_; }
    void SetGrain(float g) { grain_ = g < 0 ? 0 : g; }
    float Grain() const { return grain_; }

    void SetColor(float r, float g, float b, float a = 1.0f) { r_=r; g_=g; b_=b; a_=a; }
    void Color(float& r, float& g, float& b, float& a) const { r=r_; g=g_; b=b_; a=a_; }

    void SetName(int shaderId) { shaderId_ = shaderId; }
    int ShaderId() const { return shaderId_; }

private:
    bool enabled_ = true;
    Blend blend_ = Blend::Alpha;
    float opacity_ = 1.0f;
    float vignette_ = 0.0f;
    float distortion_ = 0.0f;
    float grain_ = 0.0f;
    float r_=0, g_=0, b_=0, a_=1;
    int shaderId_ = 0;
};

} // namespace bighero
