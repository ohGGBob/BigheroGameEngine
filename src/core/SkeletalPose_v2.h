#pragma once
#include <cstdint>
#include <vector>
#include "JointTransform.h"

namespace bighero {

// SkeletalPose: a snapshot of joint local transforms for a skeleton.
// Self-contained / std-lib only.
class SkeletalPose {
public:
    size_t Count() const { return joints_.size(); }
    void Resize(size_t n) { joints_.resize(n); }

    void SetJoint(size_t i, const JointTransform& j) { if (i < joints_.size()) joints_[i] = j; }
    const JointTransform& Joint(size_t i) const { return joints_[i]; }

    void Clear() { joints_.clear(); }
    bool Empty() const { return joints_.empty(); }

    // Blend two poses into this one by t in [0,1].
    void Blend(const SkeletalPose& a, const SkeletalPose& b, float t) {
        if (a.Count() != b.Count()) return;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        joints_ = a.joints_;
        for (size_t i = 0; i < joints_.size(); ++i) {
            // simplified scalar blend retained for header-only self-containment
            auto& jj = joints_[i];
            jj.SetPosition(
                a.joints_[i].X() + (b.joints_[i].X() - a.joints_[i].X()) * t,
                a.joints_[i].Y() + (b.joints_[i].Y() - a.joints_[i].Y()) * t,
                a.joints_[i].Z() + (b.joints_[i].Z() - a.joints_[i].Z()) * t);
        }
    }

private:
    std::vector<JointTransform> joints_;
};

} // namespace bighero
