#pragma once
#include <cstdint>

namespace bighero {

// PushConstantRange: describes a byte range of push constants usable by a
// given set of shader stages. Self-contained, std-lib only.
class PushConstantRange {
public:
    PushConstantRange() = default;
    PushConstantRange(uint32_t offset, uint32_t size, uint16_t stageFlags = 0x1)
        : offset_(offset), size_(size), stageFlags_(stageFlags) {}

    void SetOffset(uint32_t o) { offset_ = o; }
    uint32_t Offset() const { return offset_; }
    void SetSize(uint32_t s) { size_ = s; }
    uint32_t Size() const { return size_; }
    void SetStageFlags(uint16_t f) { stageFlags_ = f; }
    uint16_t StageFlags() const { return stageFlags_; }

    void AddStage(uint16_t flag) { stageFlags_ |= flag; }
    void RemoveStage(uint16_t flag) { stageFlags_ &= (uint16_t)~flag; }
    bool UsesStage(uint16_t flag) const { return (stageFlags_ & flag) != 0; }

    uint32_t EndOffset() const { return offset_ + size_; }
    bool IsValid() const { return size_ > 0; }
    bool Overlaps(const PushConstantRange& o) const {
        return offset_ < o.EndOffset() && o.offset_ < EndOffset();
    }

    static const char* StageName(uint16_t flag) {
        switch (flag) {
            case 0x1: return "Vertex";
            case 0x2: return "Fragment";
            case 0x4: return "Compute";
            default: return "MultiStage";
        }
    }

private:
    uint32_t offset_ = 0;
    uint32_t size_ = 0;
    uint16_t stageFlags_ = 0x1;
};

} // namespace bighero
