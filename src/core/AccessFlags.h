#pragma once
#include <cstdint>

namespace bighero {

// AccessFlags: bitmask of memory access types used in memory barriers.
// Self-contained, std-lib only.
class AccessFlags {
public:
    static constexpr uint32_t None = 0;
    static constexpr uint32_t Read = 1u << 0;
    static constexpr uint32_t Write = 1u << 1;
    static constexpr uint32_t ColorAttachmentRead = 1u << 2;
    static constexpr uint32_t ColorAttachmentWrite = 1u << 3;
    static constexpr uint32_t DepthStencilRead = 1u << 4;
    static constexpr uint32_t DepthStencilWrite = 1u << 5;
    static constexpr uint32_t ShaderRead = 1u << 6;
    static constexpr uint32_t ShaderWrite = 1u << 7;
    static constexpr uint32_t TransferRead = 1u << 8;
    static constexpr uint32_t TransferWrite = 1u << 9;

    static bool HasRead(uint32_t a) { return (a & (Read|ColorAttachmentRead|DepthStencilRead|ShaderRead|TransferRead)) != 0; }
    static bool HasWrite(uint32_t a) { return (a & (Write|ColorAttachmentWrite|DepthStencilWrite|ShaderWrite|TransferWrite)) != 0; }
    static bool HasShaderAccess(uint32_t a) { return (a & (ShaderRead|ShaderWrite)) != 0; }
    static bool HasTransferAccess(uint32_t a) { return (a & (TransferRead|TransferWrite)) != 0; }
};

} // namespace bighero
