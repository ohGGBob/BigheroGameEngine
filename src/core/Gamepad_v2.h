#pragma once
#include <cstdint>
#include <vector>
#include <cmath>

namespace bighero {

// Gamepad: abstracts a controller/gamepad state. Tracks buttons and axes
// (left/right stick + triggers) with edge detection, deadzone and mapping helpers.
// Self-contained, std-lib only.
class Gamepad {
public:
    enum class Button : int {
        A = 0, B = 1, X = 2, Y = 3,
        LeftBumper = 4, RightBumper = 5,
        Back = 6, Start = 7, Guide = 8,
        LeftThumb = 9, RightThumb = 10,
        DPadUp = 11, DPadRight = 12, DPadDown = 13, DPadLeft = 14,
        Count = 16
    };

    explicit Gamepad(int index = 0) : index_(index),
        buttons_(static_cast<int>(Button::Count), false),
        pressed_(static_cast<int>(Button::Count), false),
        released_(static_cast<int>(Button::Count), false) {}

    int Index() const { return index_; }

    void SetButton(Button b, bool v) {
        int i=(int)b;
        if (i>=0 && i<(int)Button::Count) {
            if (v && !buttons_[i]) pressed_[i]=true;
            if (!v && buttons_[i]) released_[i]=true;
            buttons_[i]=v;
        }
    }
    bool IsDown(Button b) const { int i=(int)b; return i>=0 && i<(int)Button::Count && buttons_[i]; }
    bool WasPressedThisFrame(Button b) const { int i=(int)b; return i>=0 && i<(int)Button::Count && pressed_[i]; }
    bool WasReleasedThisFrame(Button b) const { int i=(int)b; return i>=0 && i<(int)Button::Count && released_[i]; }

    void SetLeftStick(float x, float y) { lx_=x; ly_=y; }
    void SetRightStick(float x, float y) { rx_=x; ry_=y; }
    float LeftStickX() const { return lx_; }
    float LeftStickY() const { return ly_; }
    float RightStickX() const { return rx_; }
    float RightStickY() const { return ry_; }

    void SetTrigger(float l, float r) { lt_=l; rt_=r; }
    float LeftTrigger() const { return lt_; }
    float RightTrigger() const { return rt_; }

    // Apply radial deadzone to a stick value in place.
    static void Deadzone(float& x, float& y, float radius) {
        float d = std::sqrt(x*x + y*y);
        if (d <= radius) { x=0; y=0; return; }
        if (d > 0) { float s = (d - radius) / (1.0f - radius); x = x/d * s; y = y/d * s; }
    }

    void EndFrame() {
        std::fill(pressed_.begin(), pressed_.end(), false);
        std::fill(released_.begin(), released_.end(), false);
    }
    void Reset() { EndFrame(); std::fill(buttons_.begin(), buttons_.end(), false); lx_=ly_=rx_=ry_=lt_=rt_=0; }

private:
    int index_;
    std::vector<bool> buttons_, pressed_, released_;
    float lx_=0, ly_=0, rx_=0, ry_=0, lt_=0, rt_=0;
};

} // namespace bighero
