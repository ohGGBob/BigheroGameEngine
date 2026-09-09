#pragma once
#include <cstdint>
#include <vector>
#include "ShaderConstants_v2.h"

namespace bighero {

// PushConstantData: a typed container holding push-constant bytes to be
// written into a pipeline's push-constant range. Self-contained.
class PushConstantData {
public:
    PushConstantData() = default;
    explicit PushConstantData(uint32_t size) { data_.assign(size, 0); }

    void Resize(uint32_t size) { data_.assign(size, 0); }
    uint32_t Size() const { return (uint32_t)data_.size(); }
    bool IsEmpty() const { return data_.empty(); }

    void Set(const void* src, uint32_t byteCount) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
        if (byteCount > data_.size()) byteCount = (uint32_t)data_.size();
        for (uint32_t i = 0; i < byteCount; ++i) data_[i] = p[i];
    }
    uint8_t* Data() { return data_.data(); }
    const uint8_t* Data() const { return data_.data(); }

    void SetOffset(uint32_t offset) { offset_ = offset; }
    uint32_t Offset() const { return offset_; }
    void SetStageFlags(uint16_t flags) { stageFlags_ = flags; }
    uint16_t StageFlags() const { return stageFlags_; }

private:
    std::vector<uint8_t> data_;
    uint32_t offset_ = 0;
    uint16_t stageFlags_ = 0x1;
};

} // namespace bighero
