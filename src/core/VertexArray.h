#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// VertexArray: a CPU-side vertex-array descriptor that records the vertex
// layout, index buffer binding, and an associated vertex buffer. The renderer
// maps this to native VAO state. Self-contained, std-lib only.
class VertexArray {
public:
    // A single vertex attribute description.
    struct Attribute {
        int location = 0;
        int componentCount = 4;   // 1..4
        int offset = 0;           // byte offset into the vertex
        bool normalized = false;
        int size = 4;             // bytes per component (4 = float)
    };

    VertexArray() = default;

    void SetVertexBuffer(uint64_t bufferId, int stride, uint64_t offset = 0) {
        vertexBuffer_ = bufferId;
        stride_ = stride;
        bindingOffset_ = offset;
    }
    void SetIndexBuffer(uint64_t bufferId, int indexType = 4 /* uint32 */) {
        indexBuffer_ = bufferId;
        indexType_ = indexType;
    }
    void AddAttribute(const Attribute& attr) { attributes_.push_back(attr); }
    void ClearAttributes() { attributes_.clear(); }
    void SetElementCount(uint32_t count) { elementCount_ = count; }

    uint64_t VertexBufferId() const { return vertexBuffer_; }
    uint64_t IndexBufferId() const { return indexBuffer_; }
    int Stride() const { return stride_; }
    uint32_t ElementCount() const { return elementCount_; }
    const std::vector<Attribute>& Attributes() const { return attributes_; }
    size_t AttributeCount() const { return attributes_.size(); }
    // Total byte size of all attributes (for stride validation).
    int ComputeAttributeSize() const {
        int sz = 0;
        for (const auto& a : attributes_) sz += a.componentCount * a.size;
        return sz;
    }

private:
    uint64_t vertexBuffer_ = 0;
    uint64_t indexBuffer_ = 0;
    uint64_t bindingOffset_ = 0;
    int stride_ = 0;
    int indexType_ = 4;
    uint32_t elementCount_ = 0;
    std::vector<Attribute> attributes_;
};

} // namespace bighero
