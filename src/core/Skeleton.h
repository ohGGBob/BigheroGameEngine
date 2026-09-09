#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// Skeleton: ordered collection of bones + per-frame joint transforms.
// Self-contained / std-lib only.
class Skeleton {
public:
    size_t BoneCount() const { return bones_.size(); }
    void AddJoint(const JointTransform& j) { joints_.push_back(j); }
    const std::vector<JointTransform>& Joints() const { return joints_; }
    std::vector<JointTransform>& Joints() { return joints_; }
    size_t JointCount() const { return joints_.size(); }

    void SetJoint(size_t i, const JointTransform& j) { if (i < joints_.size()) joints_[i] = j; }
    bool SetJointByName(const std::vector<std::string>& names, size_t jointIndex, const JointTransform& j) {
        if (jointIndex < joints_.size() && jointIndex < names.size()) { joints_[jointIndex] = j; return true; }
        return false;
    }

    bool Empty() const { return joints_.empty(); }
    void Clear() { joints_.clear(); }

private:
    std::vector<JointTransform> joints_;
    // bones_ (name metadata) deliberately left to SkeletonBone registry to keep
    // this header header-only and self-contained.
    std::vector<JointTransform> bones_;
};

} // namespace bighero
