#pragma once
#include <string>
#include <vector>
#include <algorithm>

namespace bighero {

// UIPanel: a rectangular screen-space UI container with layout basics.
// Tracks visibility, z-order, anchor fractional offsets, and children.
class UIPanel {
public:
    UIPanel() {}
    explicit UIPanel(const std::string& name) : name_(name) {}

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

    void SetRect(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
    void Rect(float& x, float& y, float& w, float& h) const { x = x_; y = y_; w = w_; h = h_; }
    float Width() const { return w_; }
    float Height() const { return h_; }

    // Anchor in [0,1] relative to parent (or screen).
    void SetAnchor(float ax, float ay) { ax_ = ax; ay_ = ay; }
    void Anchor(float& ax, float& ay) const { ax = ax_; ay = ay_; }
    void SetPivot(float px, float py) { px_ = px; py_ = py; }
    void Pivot(float& px, float& py) const { px = px_; py = py_; }

    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const { return visible_; }
    void SetZOrder(int z) { z_ = z; }
    int ZOrder() const { return z_; }
    void SetOpacity(float o) { opacity_ = o < 0 ? 0 : (o > 1 ? 1 : o); }
    float Opacity() const { return opacity_; }

    // Children management.
    void AddChild(UIPanel* c) { children_.push_back(c); }
    std::size_t ChildCount() const { return children_.size(); }
    UIPanel* Child(std::size_t i) const { return i < children_.size() ? children_[i] : nullptr; }

    // Point containment (screen space).
    bool Contains(float px, float py) const {
        return px >= x_ && px <= x_ + w_ && py >= y_ && py <= y_ + h_;
    }

private:
    std::string name_;
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    float ax_ = 0.5f, ay_ = 0.5f;
    float px_ = 0.5f, py_ = 0.5f;
    bool visible_ = true;
    int z_ = 0;
    float opacity_ = 1.0f;
    std::vector<UIPanel*> children_;
};

} // namespace bighero
