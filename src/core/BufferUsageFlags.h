#pragma once
#include <cstdint>

namespace bighero {

// BufferUsageFlags: bitmask of allowed buffer usage types.
// Self-contained, std-lib only.
class BufferUsageFlags {
public:
    static constexpr uint32_t None = 0;
    static constexpr uint32_t Vertex = 1u<<0;
    static constexpr uint32_t Index = 1u<<1;
    static constexpr uint32_t Uniform = 1u<<2;
    static constexpr uint32_t Storage = 1u<<3;
    static constexpr uint32_t TransferSrc = 1u<<4;
    static constexpr uint32_t TransferDst = 1u<<5;

    static bool HasVertex(uint32_t f) { return (f&Vertex)!=0; }
    static bool HasIndex(uint32_t f) { return (f&Index)!=0; }
    static bool HasUniform(uint32_t f) { return (f&Uniform)!=0; }
    static bool HasStorage(uint32_t f) { return (f&Storage)!=0; }
    static bool HasTransferSrc(uint32_t f) { return (f&TransferSrc)!=0; }
    static bool HasTransferDst(uint32_t f) { return (f&TransferDst)!=0; }
    static bool IsGpuVisible(uint32_t f) { return (f&(Vertex|Index|Uniform|Storage))!=0; }
};

} // namespace bighero
