#pragma once
#include <cstdint>

namespace bighero {

// Keyboard key codes (subset of common keys).
enum class KeyCode : uint16_t {
    Unknown = 0,
    A=4, B=5, C=6, D=7, E=8, F=9, G=10, H=11, I=12, J=13, K=14, L=15, M=16,
    N=17, O=18, P=19, Q=20, R=21, S=22, T=23, U=24, V=25, W=26, X=27, Y=28, Z=29,
    Digit1=30, Digit2=31, Digit3=32, Digit4=33, Digit5=34, Digit6=35, Digit7=36,
    Digit8=37, Digit9=38, Digit0=39,
    Enter=40, Escape=41, Backspace=42, Tab=43, Space=44,
    Minus=45, Equals=46, LeftBracket=47, RightBracket=48, Backslash=49,
    Semicolon=51, Apostrophe=52, Grave=53, Comma=54, Period=55, Slash=56,
    LeftShift=225, RightShift=226, LeftControl=224, RightControl=228,
    LeftAlt=226, RightAlt=230, LeftSuper=227, RightSuper=231,
    Up=82, Down=81, Left=80, Right=79,
    Home=74, End=77, PageUp=75, PageDown=78, Insert=73, Delete=76,
    F1=58, F2=59, F3=60, F4=61, F5=62, F6=63, F7=64, F8=65,
    F9=66, F10=67, F11=68, F12=69
};

} // namespace bighero
