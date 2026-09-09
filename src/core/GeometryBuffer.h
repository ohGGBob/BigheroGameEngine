#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// GeometryBuffer: a deferred-rendering G-buffer descriptor — a set of render
// targets (RT ids) used to store normals, albedo, metallic/roughness, and
// world position across a frame. Pure config container.
class GeometryBuffer {
public:
    enum class Target { Albedo, Normal, MetallicRoughness, WorldPos, Count };

    GeometryBuffer() {}

    void SetTarget(Target t, std::uint64_t rtId) {
        std::size_t i = (std::size_t)t;
        if (i < (std::size_t)Target::Count) targets_[i] = rtId;
    }
    std::uint64_t TargetId(Target t) const {
        std::size_t i = (std::size_t)t;
        return i < (std::size_t)Target::Count ? targets_[i] : 0;
    }

    void SetSize(int w, int h) { w_ = w < 1 ? 1 : w; h_ = h < 1 ? 1 : h; }
    int Width() const { return w_; }
    int Height() const { return h_; }

    void SetMRChannelUsage(bool metalInR, bool roughInG) {
        metalInR_ = metalInR; roughInG_ = roughInG;
    }
    bool MetallicInRed() const { return metalInR_; }
    bool RoughnessInGreen() const { return roughInG_; }

    void SetNormalEncoding(int e) { normalEncoding_ = e; } // 0 = XYZ, 1 = octa
    int NormalEncoding() const { return normalEncoding_; }

    std::size_t TargetCount() const { return (std::size_t)Target::Count; }

private:
    std::uint64_t targets_[(std::size_t)Target::Count] = {};
    int w_ = 1, h_ = 1;
    bool metalInR_ = true, roughInG_ = true;
    int normalEncoding_ = 0;
};

} // namespace bighero
