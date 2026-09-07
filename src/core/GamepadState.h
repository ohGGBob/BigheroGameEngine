#pragma once
#include <cstdint>
#include <cmath>
#include "Input2D.h"

namespace bighero {

// Gamepad button bitmask state.
struct GamepadState {
    uint32_t buttons = 0;
    Input2D leftStick, rightStick;
    float leftTrigger = 0, rightTrigger = 0;

    static constexpr uint32_t A = 1u << 0;
    static constexpr uint32_t B = 1u << 1;
    static constexpr uint32_t X = 1u << 2;
    static constexpr uint32_t Y = 1u << 3;
    static constexpr uint32_t LB = 1u << 4;
    static constexpr uint32_t RB = 1u << 5;
    static constexpr uint32_t Back = 1u << 6;
    static constexpr uint32_t Start = 1u << 7;
    static constexpr uint32_t LeftStickClick = 1u << 8;
    static constexpr uint32_t RightStickClick = 1u << 9;
    static constexpr uint32_t DPAD_UP = 1u << 10;
    static constexpr uint32_t DPAD_DOWN = 1u << 11;
    static constexpr uint32_t DPAD_LEFT = 1u << 12;
    static constexpr uint32_t DPAD_RIGHT = 1u << 13;

    bool IsPressed(uint32_t b) const { return (buttons & b) != 0; }
    void Press(uint32_t b) { buttons |= b; }
    void Release(uint32_t b) { buttons &= ~b; }
    void ClearButtons() { buttons = 0; }
    void ClearAll() { buttons = 0; leftStick.Reset(); rightStick.Reset(); leftTrigger = rightTrigger = 0; }
};

} // namespace bighero
