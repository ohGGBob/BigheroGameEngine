#pragma once
#include <cstdint>

namespace bighero {

// PolygonMode: how polygons are rasterized in the graphics pipeline.
// Self-contained, std-lib only.
class PolygonMode {
public:
    enum class Mode : uint8_t { Fill=0, Line=1, Point=2 };

    static bool IsValid(Mode m) { return m==Mode::Fill || m==Mode::Line || m==Mode::Point; }
    static const char* Name(Mode m) {
        switch (m) { case Mode::Fill: return "Fill"; case Mode::Line: return "Line"; case Mode::Point: return "Point"; }
        return "Unknown";
    }
    static bool IsWireframe(Mode m) { return m==Mode::Line; }
    static bool UsesPoints(Mode m) { return m==Mode::Point; }
};

} // namespace bighero
