#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace bighero {

// UniformBlock: describes a named block of uniform data (name + size) used to
// bind CPU-side data to GPU shader uniforms. Self-contained.
class UniformBlock {
public:
    UniformBlock() = default;
    UniformBlock(std::string name, uint32_t binding = 0, uint32_t size = 0)
        : name_(std::move(name)), binding_(binding), size_(size) {}

    const std::string& Name() const { return name_; }
    void SetName(std::string n) { name_ = std::move(n); }
    void SetBinding(uint32_t b) { binding_ = b; }
    uint32_t Binding() const { return binding_; }
    void SetSize(uint32_t s) { size_ = s; }
    uint32_t Size() const { return size_; }

    void SetData(const void* data, uint32_t byteCount) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        data_.assign(p, p + byteCount);
        size_ = byteCount;
    }
    const std::vector<uint8_t>& Data() const { return data_; }
    uint8_t* DataPtr() { return data_.data(); }
    const uint8_t* DataPtr() const { return data_.data(); }
    bool IsBound() const { return !data_.empty(); }

private:
    std::string name_;
    uint32_t binding_ = 0;
    uint32_t size_ = 0;
    std::vector<uint8_t> data_;
};

} // namespace bighero
