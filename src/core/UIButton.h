#pragma once
#include <string>
#include <functional>

namespace bighero {

// UIButton: a clickable UI control with hover/press state and an optional
// click callback. Pure input-state tracking; no rendering backend.
class UIButton {
public:
    enum class State { Normal, Hover, Pressed, Disabled };

    UIButton() {}
    explicit UIButton(const std::string& text) : text_(text) {}

    void SetText(const std::string& t) { text_ = t; }
    const std::string& Text() const { return text_; }

    void SetBounds(float x, float y, float w, float h) {
        x_ = x; y_ = y; w_ = w; h_ = h;
    }
    bool Contains(float px, float py) const {
        return px >= x_ && px <= x_ + w_ && py >= y_ && py <= y_ + h_;
    }
    float Width() const { return w_; }
    float Height() const { return h_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Feed pointer state; triggers click callback on press+finger up.
    bool OnPointerDown(float px, float py) {
        pointerDown_ = true;
        if (!enabled_) { state_ = State::Disabled; return false; }
        if (Contains(px, py)) { state_ = State::Pressed; return true; }
        state_ = State::Normal;
        return false;
    }
    bool OnPointerUp(float px, float py) {
        bool clicked = false;
        if (pointerDown_ && enabled_ && Contains(px, py)) {
            clicked = true;
            if (onClick_) onClick_();
        }
        pointerDown_ = false;
        state_ = enabled_ ? State::Normal : State::Disabled;
        return clicked;
    }
    void OnPointerMove(float px, float py) {
        if (!enabled_) { state_ = State::Disabled; return; }
        if (pointerDown_) { if (Contains(px, py)) state_ = State::Pressed; else state_ = State::Normal; }
        else { state_ = Contains(px, py) ? State::Hover : State::Normal; }
    }

    State CurrentState() const { return state_; }
    void SetOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }

private:
    std::string text_;
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    bool enabled_ = true;
    bool pointerDown_ = false;
    State state_ = State::Normal;
    std::function<void()> onClick_;
};

} // namespace bighero
