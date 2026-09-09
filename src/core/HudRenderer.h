#pragma once
#include <vector>
#include <string>
#include <cstddef>

namespace bighero {

// HudRenderer: collects HUD element draw requests (text, rects, bars) for the
// UI backend. Batch command-list container with a simple batching structure.
class HudRenderer {
public:
    enum class Prim { Text, Rect, Bar, Circle };

    struct Element {
        Prim prim;
        float x, y, w, h;
        float r, g, b, a;
        std::string text;   // for text prims
        int layer;          // draw layer/order
    };

    HudRenderer() {}
    void Clear() { elements_.clear(); }

    void SetColor(float r, float g, float b, float a = 1.0f) {
        r_=r; g_=g; b_=b; a_=a;
    }
    void SetLayer(int layer) { layer_ = layer; }

    void DrawText(const std::string& t, float x, float y, float h = 16.0f) {
        Element e; e.prim=Prim::Text; e.x=x; e.y=y; e.w=0; e.h=h;
        e.r=r_; e.g=g_; e.b=b_; e.a=a_; e.text=t; e.layer=layer_;
        elements_.push_back(e);
    }
    void DrawRect(float x, float y, float w, float h) {
        Element e; e.prim=Prim::Rect; e.x=x; e.y=y; e.w=w; e.h=h;
        e.r=r_; e.g=g_; e.b=b_; e.a=a_; e.layer=layer_;
        elements_.push_back(e);
    }
    void DrawBar(float x, float y, float w, float h, float frac) {
        Element e; e.prim=Prim::Bar; e.x=x; e.y=y; e.w=w*Clamp01(frac); e.h=h;
        e.r=r_; e.g=g_; e.b=b_; e.a=a_; e.layer=layer_;
        elements_.push_back(e);
    }

    std::size_t Count() const { return elements_.size(); }
    const Element& At(std::size_t i) const { return elements_[i]; }
    bool Empty() const { return elements_.empty(); }

    // Number of text elements (for stats).
    std::size_t TextCount() const {
        std::size_t n = 0;
        for (auto& e : elements_) if (e.prim == Prim::Text) ++n;
        return n;
    }

private:
    static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    std::vector<Element> elements_;
    float r_=1, g_=1, b_=1, a_=1;
    int layer_ = 0;
};

} // namespace bighero
