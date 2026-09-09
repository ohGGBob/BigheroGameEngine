#pragma once
#include <cstdint>

namespace bighero {

// VertexInputBinding: describes a vertex buffer binding (binding slot, stride,
// and input rate). Self-contained, std-lib only.
class VertexInputBinding {
public:
    enum class InputRate : uint8_t { Vertex = 0, Instance = 1 };

    VertexInputBinding() = default;
    VertexInputBinding(uint32_t binding, uint32_t stride, InputRate rate = InputRate::Vertex)
        : binding_(binding), stride_(stride), rate_(rate) {}

    void SetBinding(uint32_t b) { binding_ = b; }
    uint32_t Binding() const { return binding_; }
    void SetStride(uint32_t s) { stride_ = s; }
    uint32_t Stride() const { return stride_; }
    void SetInputRate(InputRate r) { rate_ = r; }
    InputRate GetInputRate() const { return rate_; }

    bool IsPerVertex() const { return rate_ == InputRate::Vertex; }
    bool IsPerInstance() const { return rate_ == InputRate::Instance; }
    bool IsValid() const { return stride_ > 0; }

    static const char* RateName(InputRate r) {
        switch (r) {
            case InputRate::Vertex: return "Vertex";
            case InputRate::Instance: return "Instance";
        }
        return "Unknown";
    }

private:
    uint32_t binding_ = 0;
    uint32_t stride_ = 0;
    InputRate rate_ = InputRate::Vertex;
};

} // namespace bighero
