#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// QueueFamilyIndicesDescriptor: holds indices of queue families used by a
// resource (graphics, compute, transfer, present). Self-contained.
class QueueFamilyIndicesDescriptor {
public:
    QueueFamilyIndicesDescriptor() = default;

    void SetGraphics(uint32_t i) { graphics_ = i; hasGraphics_ = true; }
    bool HasGraphics() const { return hasGraphics_; }
    uint32_t Graphics() const { return graphics_; }

    void SetCompute(uint32_t i) { compute_ = i; hasCompute_ = true; }
    bool HasCompute() const { return hasCompute_; }
    uint32_t Compute() const { return compute_; }

    void SetTransfer(uint32_t i) { transfer_ = i; hasTransfer_ = true; }
    bool HasTransfer() const { return hasTransfer_; }
    uint32_t Transfer() const { return transfer_; }

    void SetPresent(uint32_t i) { present_ = i; hasPresent_ = true; }
    bool HasPresent() const { return hasPresent_; }
    uint32_t Present() const { return present_; }

    void Reset() { hasGraphics_=hasCompute_=hasTransfer_=hasPresent_=false; }

    bool IsComplete() const { return hasGraphics_ && hasCompute_ && hasTransfer_; }
    uint32_t UniqueFamilyCount() const {
        std::vector<uint32_t> v;
        if (hasGraphics_) v.push_back(graphics_);
        if (hasCompute_) v.push_back(compute_);
        if (hasTransfer_) v.push_back(transfer_);
        if (hasPresent_) v.push_back(present_);
        uint32_t n = 0;
        for (size_t i=0;i<v.size();++i) {
            bool dup=false;
            for (size_t j=0;j<i;++j) if (v[j]==v[i]) dup=true;
            if (!dup) ++n;
        }
        return n;
    }
    bool AllSameFamily() const {
        return IsComplete() &&
               graphics_==compute_ && graphics_==transfer_ &&
               (!hasPresent_ || graphics_==present_);
    }

private:
    bool hasGraphics_=false, hasCompute_=false, hasTransfer_=false, hasPresent_=false;
    uint32_t graphics_=0, compute_=0, transfer_=0, present_=0;
};

} // namespace bighero
