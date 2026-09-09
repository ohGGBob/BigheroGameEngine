#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace bighero {

// SkeletonBone: node definition within a skeleton (name + parent index).
// Self-contained / std-lib only.
class SkeletonBone {
public:
    SkeletonBone() = default;
    SkeletonBone(std::string name, int parentIndex)
        : name_(std::move(name)), parentIndex_(parentIndex) {}

    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }
    void SetParentIndex(int i) { parentIndex_ = i; }
    int ParentIndex() const { return parentIndex_; }
    bool HasParent() const { return parentIndex_ >= 0; }

    void SetBindPose(float tx, float ty, float tz, float qw, float qx, float qy, float qz) {
        tx_=tx; ty_=ty; tz_=tz; qw_=qw; qx_=qx; qy_=qy; qz_=qz;
    }
    bool IsRoot() const { return parentIndex_ < 0; }

private:
    std::string name_;
    int parentIndex_ = -1;
    float tx_=0, ty_=0, tz_=0, qw_=1, qx_=0, qy_=0, qz_=0;
};

} // namespace bighero
