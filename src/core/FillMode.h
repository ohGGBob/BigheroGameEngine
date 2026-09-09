#pragma once
#include <cstdint>

namespace bighero {

// FillMode: how fragments are filled in the rasterizer.
// Self-contained, std-lib only.
class FillMode {
public:
    enum class Mode : uint8_t { Solid=0, Wireframe=1, Point=2 };

    static bool IsValid(Mode m) { return m<=Mode::Point; }
    static bool IsSolid(Mode m) { return m==Mode::Solid; }
    static bool IsWireframe(Mode m) { return m==Mode::Wireframe; }
    static const char* Name(Mode m) {
        switch (m) { case Mode::Solid: return "Solid"; case Mode::Wireframe: return "Wireframe"; case Mode::Point: return "Point"; }
        return "Unknown";
    }
};

} // namespace bighero
