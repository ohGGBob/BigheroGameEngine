#pragma once
#include <vector>
#include <cstddef>
#include <string>

namespace bighero {

// AnimationLayer: a layer in an animation controller that selects one clip
// weight and an optional additive mode. Mirrors simple layered-animation
// structure. Pure config container.
class AnimationLayer {
public:
    AnimationLayer() {}
    AnimationLayer(const std::string& name) : name_(name) {}

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }
    void SetWeight(float w) { weight_ = w < 0 ? 0 : (w > 1 ? 1 : w); }
    float Weight() const { return weight_; }
    void SetIndex(int i) { index_ = i; }
    int Index() const { return index_; }
    void SetAdditive(bool a) { additive_ = a; }
    bool IsAdditive() const { return additive_; }

    void SetMask(std::size_t boneMask) { mask_ = boneMask; }
    std::size_t Mask() const { return mask_; }
    std::size_t AddBoneToMask(std::size_t bit) { mask_ |= (std::size_t(1) << bit); return mask_; }
    bool MaskHasBone(std::size_t bit) const { return (mask_ >> bit) & 1; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    std::string name_;
    float weight_ = 1.0f;
    int index_ = 0;
    bool additive_ = false;
    std::size_t mask_ = 0;
    bool enabled_ = true;
};

} // namespace bighero
