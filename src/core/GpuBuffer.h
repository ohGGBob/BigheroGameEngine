#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>

namespace bighero {

// GPU buffer handle descriptor covering vertex/index/uniform/storage usages.
// CPU-side bookkeeping; the actual buffer object lives in the backend.
class GpuBuffer {
public:
    enum class Type { Vertex, Index, Uniform, Storage };
    enum class Usage { Default, Static, Dynamic, Stream };

    GpuBuffer() {}
    GpuBuffer(Type type, Usage usage = Usage::Static)
        : type_(type), usage_(usage) {}

    void SetType(Type t) { type_ = t; }
    Type BufferType() const { return type_; }
    void SetUsage(Usage u) { usage_ = u; }
    Usage UsageMode() const { return usage_; }
    void SetHandle(std::uint64_t h) { handle_ = h; }
    std::uint64_t Handle() const { return handle_; }

    void SetByteSize(std::size_t bytes) { byteSize_ = bytes; }
    std::size_t ByteSize() const { return byteSize_; }
    void MarkDirty() { dirty_ = true; }
    void MarkClean() { dirty_ = false; }
    bool Dirty() const { return dirty_; }

    bool IsValid() const { return handle_ != 0 && byteSize_ > 0; }

private:
    Type type_ = Type::Vertex;
    Usage usage_ = Usage::Static;
    std::uint64_t handle_ = 0;
    std::size_t byteSize_ = 0;
    bool dirty_ = true;
};

} // namespace bighero
