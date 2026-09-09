#pragma once
#include <cstdint>

namespace bighero {

// CullMode: back/front face culling mode. Self-contained, std-lib only.
class CullMode {
public:
    enum class Mode : uint8_t { None=0, Front=1, Back=2, FrontAndBack=3 };

    static bool IsValid(Mode m) { return m<=Mode::FrontAndBack; }
    static bool CullsBack(Mode m) { return m==Mode::Back || m==Mode::FrontAndBack; }
    static bool CullsFront(Mode m) { return m==Mode::Front || m==Mode::FrontAndBack; }
    static bool CullsAnything(Mode m) { return m!=Mode::None; }
    static const char* Name(Mode m) {
        switch (m) { case Mode::None: return "None"; case Mode::Front: return "Front"; case Mode::Back: return "Back"; case Mode::FrontAndBack: return "FrontAndBack"; }
        return "Unknown";
    }
};

} // namespace bighero
