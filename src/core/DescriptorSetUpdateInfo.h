#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// DescriptorSetUpdateInfo: describes an update to a descriptor set (a typed
// write of one binding). Self-contained, std-lib only.
class DescriptorSetUpdateInfo {
public:
    enum class Type : uint8_t { UniformBuffer=0, SampledImage=1, StorageBuffer=2, Sampler=3, CombinedImageSampler=4 };

    DescriptorSetUpdateInfo() = default;
    DescriptorSetUpdateInfo(uint32_t binding, Type type) : binding_(binding), type_(type) {}

    void SetBinding(uint32_t b) { binding_ = b; }
    uint32_t Binding() const { return binding_; }
    void SetType(Type t) { type_ = t; }
    Type GetType() const { return type_; }
    void SetCount(uint32_t c) { count_ = c; }
    uint32_t Count() const { return count_; }
    void SetArrayElement(uint32_t e) { arrayElement_ = e; }
    uint32_t ArrayElement() const { return arrayElement_; }

    bool IsValid() const { return binding_ != 0xFFFFFFFFu && count_ > 0; }
    static const char* TypeName(Type t) {
        switch (t) {
            case Type::UniformBuffer: return "UniformBuffer";
            case Type::SampledImage: return "SampledImage";
            case Type::StorageBuffer: return "StorageBuffer";
            case Type::Sampler: return "Sampler";
            case Type::CombinedImageSampler: return "CombinedImageSampler";
        }
        return "Unknown";
    }

private:
    uint32_t binding_ = 0xFFFFFFFFu;
    uint32_t arrayElement_ = 0, count_ = 1;
    Type type_ = Type::UniformBuffer;
};

} // namespace bighero
