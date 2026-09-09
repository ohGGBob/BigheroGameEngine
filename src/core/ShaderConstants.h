#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace bighero {

// ShaderConstants: a small block of scalar/vector constants passed directly to
// a shader stage (push constants). Self-contained, std-lib only.
class ShaderConstants {
public:
    ShaderConstants() = default;
    ShaderConstants(uint32_t size, uint16_t stageFlags = 0x1)
        : size_(size), stageFlags_(stageFlags) {
        data_.assign(size, 0);
    }

    void Allocate(uint32_t size) {
        size_ = size;
        data_.assign(size, 0);
    }
    uint32_t Size() const { return size_; }
    void SetStageFlags(uint16_t f) { stageFlags_ = f; }
    uint16_t StageFlags() const { return stageFlags_; }

    void SetData(const void* src, uint32_t byteCount) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
        if (byteCount > data_.size()) byteCount = (uint32_t)data_.size();
        for (uint32_t i = 0; i < byteCount; ++i) data_[i] = p[i];
        if (byteCount < data_.size()) {
            for (uint32_t i = byteCount; i < data_.size(); ++i) data_[i] = 0;
        }
    }
    uint8_t* DataPtr() { return data_.data(); }
    const uint8_t* DataPtr() const { return data_.data(); }
    bool IsEmpty() const { return data_.empty(); }

    void SetFloat(uint32_t offset, float v) {
        if (offset + 4 > data_.size()) return;
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        data_[offset]=(bits>>0)&0xFF; data_[offset+1]=(bits>>8)&0xFF;
        data_[offset+2]=(bits>>16)&0xFF; data_[offset+3]=(bits>>24)&0xFF;
    }
    float GetFloat(uint32_t offset) const {
        if (offset + 4 > data_.size()) return 0;
        uint32_t bits = data_[offset] | (data_[offset+1]<<8) | (data_[offset+2]<<16) | ((uint32_t)data_[offset+3]<<24);
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }

private:
    uint32_t size_ = 0;
    uint16_t stageFlags_ = 0x1;
    std::vector<uint8_t> data_;
};

} // namespace bighero
