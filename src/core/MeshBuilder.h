#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace bighero {

// Accumulates a triangle mesh (positions/normals/uvs/indices) on the CPU and
// produces a tightly packed vertex buffer for upload. Useful for procedural
// geometry generation.
class MeshBuilder {
public:
    void Clear() {
        pos_.clear(); normal_.clear(); uv_.clear(); color_.clear(); indices_.clear();
    }
    std::size_t VertexCount() const { return pos_.size() / 3; }
    std::size_t IndexCount() const { return indices_.size(); }
    bool Empty() const { return pos_.empty(); }

    void AddVertex(float px, float py, float pz,
                   float nx = 0, float ny = 0, float nz = 1,
                   float u = 0, float v = 0,
                   uint32_t color = 0xFFFFFFFFu) {
        pos_.push_back(px); pos_.push_back(py); pos_.push_back(pz);
        normal_.push_back(nx); normal_.push_back(ny); normal_.push_back(nz);
        uv_.push_back(u); uv_.push_back(v);
        color_.push_back(color);
    }

    void AddTriangle(uint32_t a, uint32_t b, uint32_t c) {
        indices_.push_back(a); indices_.push_back(b); indices_.push_back(c);
    }

    void AddQuad(float cx, float cy, float w, float h) {
        uint32_t base = (uint32_t)(pos_.size() / 3);
        float hw = w * 0.5f, hh = h * 0.5f;
        AddVertex(cx - hw, cy - hh, 0, 0, 0, 1, 0, 0);
        AddVertex(cx + hw, cy - hh, 0, 0, 0, 1, 1, 0);
        AddVertex(cx + hw, cy + hh, 0, 0, 0, 1, 1, 1);
        AddVertex(cx - hw, cy + hh, 0, 0, 0, 1, 0, 1);
        AddTriangle(base, base + 1, base + 2);
        AddTriangle(base, base + 2, base + 3);
    }

    const std::vector<float>& Positions() const { return pos_; }
    const std::vector<float>& Normals() const { return normal_; }
    const std::vector<float>& UVs() const { return uv_; }
    const std::vector<uint32_t>& Colors() const { return color_; }
    const std::vector<uint32_t>& Indices() const { return indices_; }

    struct VertexData {
        std::vector<uint8_t> bytes;
        std::size_t stride = 0;
    };

    // Interleaved vertex data: pos(3f) normal(3f) uv(2f) color(4u8) = 36 bytes.
    VertexData BuildInterleaved() const {
        VertexData out;
        out.stride = 12 + 12 + 8 + 4; // 36
        std::size_t count = pos_.size() / 3;
        out.bytes.reserve(count * out.stride);
        auto append = [&](const void* p, std::size_t n) {
            const uint8_t* b = (const uint8_t*)p;
            out.bytes.insert(out.bytes.end(), b, b + n);
        };
        for (std::size_t i = 0; i < count; ++i) {
            float p[3] = {pos_[i*3], pos_[i*3+1], pos_[i*3+2]};
            float n[3] = {normal_[i*3], normal_[i*3+1], normal_[i*3+2]};
            float u[2] = {uv_[i*2], uv_[i*2+1]};
            uint32_t c = color_[i];
            append(p, 12);
            append(n, 12);
            append(u, 8);
            append(&c, 4);
        }
        return out;
    }

    // Build index buffer bytes (u32).
    std::vector<uint8_t> BuildIndices() const {
        std::vector<uint8_t> out;
        out.reserve(indices_.size() * 4);
        for (uint32_t i : indices_) {
            for (int k = 0; k < 4; ++k) out.push_back((uint8_t)((i >> (k * 8)) & 0xFF));
        }
        return out;
    }

private:
    std::vector<float> pos_, normal_, uv_;
    std::vector<uint32_t> color_;
    std::vector<uint32_t> indices_;
};

} // namespace bighero
