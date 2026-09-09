#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// Mouse: abstracts mouse state. Tracks position, delta, per-button press/release
// edges, and scroll delta. Self-contained, std-lib only.
class Mouse {
public:
    enum class Button : int {
        Left = 0, Right = 1, Middle = 2,
        Button4 = 3, Button5 = 4, Button6 = 5, Button7 = 6, Count = 8
    };

    Mouse() : down_(static_cast<int>(Button::Count), false),
              pressed_(static_cast<int>(Button::Count), false),
              released_(static_cast<int>(Button::Count), false) {}

    void SetPosition(float x, float y, float dx, float dy) { x_=x; y_=y; dx_=dx; dy_=dy; }
    void SetDelta(float dx, float dy) { dx_=dx; dy_=dy; }
    void SetScroll(float sx, float sy) { scrollX_=sx; scrollY_=sy; }
    void AddScroll(float sx, float sy) { scrollX_+=sx; scrollY_+=sy; }

    float X() const { return x_; }
    float Y() const { return y_; }
    float DeltaX() const { return dx_; }
    float DeltaY() const { return dy_; }
    float ScrollX() const { return scrollX_; }
    float ScrollY() const { return scrollY_; }

    void Press(Button b) {
        int i=(int)b;
        if (i>=0 && i<(int)Button::Count) { if (!down_[i]) pressed_[i]=true; down_[i]=true; }
    }
    void Release(Button b) {
        int i=(int)b;
        if (i>=0 && i<(int)Button::Count) { if (down_[i]) released_[i]=true; down_[i]=false; }
    }
    bool IsDown(Button b) const { int i=(int)b; return i>=0 && i<(int)Button::Count && down_[i]; }
    bool IsUp(Button b) const { return !IsDown(b); }
    bool WasPressedThisFrame(Button b) const { int i=(int)b; return i>=0 && i<(int)Button::Count && pressed_[i]; }
    bool WasReleasedThisFrame(Button b) const { int i=(int)b; return i>=0 && i<(int)Button::Count && released_[i]; }
    bool IsAnyDown() const { for (bool v : down_) if (v) return true; return false; }

    void EndFrame() {
        dx_=0; dy_=0; scrollX_=0; scrollY_=0;
        std::fill(pressed_.begin(), pressed_.end(), false);
        std::fill(released_.begin(), released_.end(), false);
    }
    void Reset() { EndFrame(); std::fill(down_.begin(), down_.end(), false); }

private:
    float x_=0, y_=0, dx_=0, dy_=0, scrollX_=0, scrollY_=0;
    std::vector<bool> down_, pressed_, released_;
};

} // namespace bighero
