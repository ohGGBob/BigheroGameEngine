#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// VertexAttribute: a flat attribute element bound to a semantic location,
// used for per-vertex data (position/normal/uv/color) with helper accessors.
// Self-contained, std-lib only.
class VertexAttribute {
public:
    VertexAttribute() = default;

    void SetLocation(int loc) { location_ = loc; }
    int Location() const { return location_; }

    void SetType(int type) { type_ = type; }   // 0=float,1=float2,2=float3,3=float4
    int Type() const { return type_; }
    int ComponentCount() const {
        switch (type_) { case 0: return 1; case 1: return 2; case 2: return 3; case 3: return 4; default: return 1; }
    }

    void SetData(const float* vals, int count) {
        data_.assign(vals, vals + count);
    }
    // Copy the value for a given component into a scalar.
    float GetComponent(int index) const {
        if (index < 0 || (size_t)index >= data_.size()) return 0;
        return data_[(size_t)index];
    }
    size_t Size() const { return data_.size(); }

    // Convenience helpers to populate a 3-component position/normal.
    void SetFloat3(float x, float y, float z) {
        type_ = 2; data_ = {x, y, z};
    }
    void SetFloat2(float x, float y) {
        type_ = 1; data_ = {x, y};
    }
    void SetFloat4(float x, float y, float z, float w) {
        type_ = 3; data_ = {x, y, z, w};
    }

private:
    int location_ = 0;
    int type_ = 0;
    std::vector<float> data_;
};

} // namespace bighero
