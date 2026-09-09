#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// Keyboard: abstracts keyboard state. Tracks pressed/released/held keys with
// edge detection (WasPressedThisFrame / WasReleasedThisFrame).
// Self-contained, std-lib only.
class Keyboard {
public:
    enum class Key : int {
        Space = 32, Quote = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
        Digit0 = 48, Digit1 = 49, Digit2 = 50, Digit3 = 51, Digit4 = 52,
        Digit5 = 53, Digit6 = 54, Digit7 = 55, Digit8 = 56, Digit9 = 57,
        Semicolon = 59, Equal = 61,
        A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72, I = 73,
        J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80, Q = 81, R = 82,
        S = 83, T = 84, U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,
        LeftBracket = 91, Backslash = 92, RightBracket = 93,
        Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260,
        Delete = 261, Right = 262, Left = 263, Down = 264, Up = 265,
        PageUp = 266, PageDown = 267, Home = 268, End = 269,
        CapsLock = 280, ScrollLock = 281, NumLock = 282, PrintScreen = 283, Pause = 284,
        F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295, F7 = 296,
        F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301,
        LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
        RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347,
        Count = 512
    };

    Keyboard() : down_(static_cast<int>(Key::Count), false),
                 pressed_(static_cast<int>(Key::Count), false),
                 released_(static_cast<int>(Key::Count), false) {}

    void Press(Key k) {
        int i = static_cast<int>(k);
        if (i >= 0 && i < (int)Key::Count) {
            if (!down_[i]) pressed_[i] = true;
            down_[i] = true;
        }
    }
    void Release(Key k) {
        int i = static_cast<int>(k);
        if (i >= 0 && i < (int)Key::Count) {
            if (down_[i]) released_[i] = true;
            down_[i] = false;
        }
    }
    bool IsDown(Key k) const { int i=(int)k; return i>=0 && i<(int)Key::Count && down_[i]; }
    bool IsUp(Key k) const { return !IsDown(k); }
    bool WasPressedThisFrame(Key k) const { int i=(int)k; return i>=0 && i<(int)Key::Count && pressed_[i]; }
    bool WasReleasedThisFrame(Key k) const { int i=(int)k; return i>=0 && i<(int)Key::Count && released_[i]; }

    // Clear per-frame edge state (call once per frame).
    void EndFrame() {
        std::fill(pressed_.begin(), pressed_.end(), false);
        std::fill(released_.begin(), released_.end(), false);
    }
    void Reset() { EndFrame(); std::fill(down_.begin(), down_.end(), false); }

    static const char* KeyName(Key k) {
        switch (k) {
            case Key::Space: return "Space"; case Key::Enter: return "Enter";
            case Key::Escape: return "Escape"; case Key::Tab: return "Tab";
            case Key::A: return "A"; case Key::D: return "D"; case Key::W: return "W";
            case Key::S: return "S"; case Key::LeftShift: return "LeftShift";
            case Key::LeftControl: return "LeftControl"; case Key::LeftAlt: return "LeftAlt";
            default: return "Unknown";
        }
    }

private:
    std::vector<bool> down_, pressed_, released_;
};

} // namespace bighero
