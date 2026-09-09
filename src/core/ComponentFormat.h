#pragma once
#include <cstdint>

namespace bighero {

// ComponentFormat: data format of a vertex attribute / texel.
// Self-contained, std-lib only.
class ComponentFormat {
public:
    enum class Format : uint8_t { R32Float=0, R32G32Float=1, R32G32B32Float=2, R32G32B32A32Float=3, R8UNorm=4, R8G8B8A8UNorm=5, R16Float=6, R16G16B16A16Float=7, R16Uint=8, R32Uint=9 };

    static uint32_t ComponentCount(Format f) {
        switch (f) {
            case Format::R32Float: case Format::R8UNorm: case Format::R16Float: case Format::R16Uint: case Format::R32Uint: return 1;
            case Format::R32G32Float: return 2;
            case Format::R32G32B32Float: return 3;
            default: return 4;
        }
    }
    static uint32_t ByteSize(Format f) {
        switch (f) {
            case Format::R32Float: case Format::R32Uint: return 4;
            case Format::R32G32Float: return 8;
            case Format::R32G32B32Float: return 12;
            case Format::R32G32B32A32Float: return 16;
            case Format::R8UNorm: return 1;
            case Format::R8G8B8A8UNorm: return 4;
            case Format::R16Float: case Format::R16Uint: return 2;
            case Format::R16G16B16A16Float: return 8;
        }
        return 0;
    }
    static bool IsValid(Format f) { return f<=Format::R32Uint; }
    static bool IsFloat(Format f) { return f<=Format::R32G32B32A32Float; }
    static const char* Name(Format f) {
        switch (f) {
            case Format::R32Float: return "R32Float"; case Format::R32G32Float: return "R32G32Float";
            case Format::R32G32B32Float: return "R32G32B32Float"; case Format::R32G32B32A32Float: return "R32G32B32A32Float";
            case Format::R8UNorm: return "R8UNorm"; case Format::R8G8B8A8UNorm: return "R8G8B8A8UNorm";
            case Format::R16Float: return "R16Float"; case Format::R16G16B16A16Float: return "R16G16B16A16Float";
            case Format::R16Uint: return "R16Uint"; case Format::R32Uint: return "R32Uint";
        }
        return "Unknown";
    }
};

} // namespace bighero
