#pragma once
#include <cstdint>

namespace bighero {

// FrontFace: winding order that defines the front face. Self-contained,
// std-lib only.
class FrontFace {
public:
    enum class Winding : uint8_t { CounterClockwise=0, Clockwise=1 };

    static bool IsValid(Winding w) { return w==Winding::CounterClockwise || w==Winding::Clockwise; }
    static bool IsCounterClockwise(Winding w) { return w==Winding::CounterClockwise; }
    static const char* Name(Winding w) {
        switch (w) { case Winding::CounterClockwise: return "CounterClockwise"; case Winding::Clockwise: return "Clockwise"; }
        return "Unknown";
    }
};

} // namespace bighero
