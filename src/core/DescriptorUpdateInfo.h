#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// DescriptorUpdateInfo: describes a set of descriptor writes (buffer, sampler,
// image) to update a descriptor set. Self-contained, std-lib only.
class DescriptorUpdateInfo {
public:
    enum class Type : uint8_t { UniformBuffer = 0, CombinedSampler = 1, StorageBuffer = 2, Texture = 3, Sampler = 4 };

    struct DescriptorValue {
        uint64_t handle = 0;   // buffer or image handle
        uint32_t offset = 0;
        uint32_t range = 0;
    };

    DescriptorUpdateInfo() = default;

    void SetDescriptorSet(uint32_t ds) { descriptorSet_ = ds; }
    uint32_t DescriptorSet() const { return descriptorSet_; }
    void SetBinding(uint32_t b) { binding_ = b; }
    uint32_t Binding() const { return binding_; }
    void SetType(Type t) { type_ = t; }
    Type GetType() const { return type_; }

    void SetValue(uint64_t handle, uint32_t offset = 0, uint32_t range = 0) {
        DescriptorValue v; v.handle = handle; v.offset = offset; v.range = range;
        value_ = v;
    }
    const DescriptorValue& Value() const { return value_; }
    bool IsValid() const { return value_.handle != 0; }

    static const char* TypeName(Type t) {
        switch (t) {
            case Type::UniformBuffer: return "UniformBuffer";
            case Type::CombinedSampler: return "CombinedSampler";
            case Type::StorageBuffer: return "StorageBuffer";
            case Type::Texture: return "Texture";
            case Type::Sampler: return "Sampler";
        }
        return "Unknown";
    }

private:
    uint32_t descriptorSet_ = 0;
    uint32_t binding_ = 0;
    Type type_ = Type::UniformBuffer;
    DescriptorValue value_;
};

} // namespace bighero
