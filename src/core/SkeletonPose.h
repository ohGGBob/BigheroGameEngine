#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// SkeletonPose: a full pose for a skeleton — per-bone local transforms stored
// as translation + quaternion (qx,qy,qz,qw). Self-contained data container
// used by animation and IK; rotation helpers keep it standard-library only.
class SkeletonPose {
public:
    struct BonePose { float tx,ty,tz, qx,qy,qz,qw; };

    explicit SkeletonPose(std::size_t boneCount = 0) { ResizeBones(boneCount); }

    void ResizeBones(std::size_t n) { bones_.assign(n, BonePose{0,0,0, 0,0,0,1}); }
    std::size_t BoneCount() const { return bones_.size(); }
    bool SetBone(std::size_t i, float tx,float ty,float tz,
                 float qx,float qy,float qz,float qw) {
        if (i >= bones_.size()) return false;
        bones_[i] = {tx,ty,tz, qx,qy,qz,qw};
        return true;
    }
    bool GetBone(std::size_t i, BonePose& out) const {
        if (i >= bones_.size()) return false;
        out = bones_[i]; return true;
    }
    void SetBoneRotation(std::size_t i, float qx,float qy,float qz,float qw) {
        if (i >= bones_.size()) return;
        float len = std::sqrt(qx*qx+qy*qy+qz*qz+qw*qw);
        if (len > 1e-8f) { bones_[i].qx=qx/len; bones_[i].qy=qy/len; bones_[i].qz=qz/len; bones_[i].qw=qw/len; }
        else { bones_[i].qx=0; bones_[i].qy=0; bones_[i].qz=0; bones_[i].qw=1; }
    }
    void SetBoneTranslation(std::size_t i, float tx,float ty,float tz) {
        if (i >= bones_.size()) return;
        bones_[i].tx=tx; bones_[i].ty=ty; bones_[i].tz=tz;
    }

    void Blend(const SkeletonPose& a, const SkeletonPose& b, float t) {
        std::size_t n = a.BoneCount() < b.BoneCount() ? a.BoneCount() : b.BoneCount();
        ResizeBones(n);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        for (std::size_t i = 0; i < n; ++i) {
            BonePose pa, pb;
            a.GetBone(i, pa); b.GetBone(i, pb);
            bones_[i].tx = pa.tx + (pb.tx-pa.tx)*t;
            bones_[i].ty = pa.ty + (pb.ty-pa.ty)*t;
            bones_[i].tz = pa.tz + (pb.tz-pa.tz)*t;
            // nlerp rotation
            float dot = pa.qx*pb.qx + pa.qy*pb.qy + pa.qz*pb.qz + pa.qw*pb.qw;
            if (dot < 0) { pb.qx=-pb.qx; pb.qy=-pb.qy; pb.qz=-pb.qz; pb.qw=-pb.qw; }
            float ix = pa.qx + (pb.qx-pa.qx)*t;
            float iy = pa.qy + (pb.qy-pa.qy)*t;
            float iz = pa.qz + (pb.qz-pa.qz)*t;
            float iw = pa.qw + (pb.qw-pa.qw)*t;
            float len = std::sqrt(ix*ix+iy*iy+iz*iz+iw*iw);
            if (len > 1e-8f) { bones_[i].qx=ix/len; bones_[i].qy=iy/len; bones_[i].qz=iz/len; bones_[i].qw=iw/len; }
            else { bones_[i].qx=0; bones_[i].qy=0; bones_[i].qz=0; bones_[i].qw=1; }
        }
    }

    void Reset() {
        for (auto& b : bones_) b = BonePose{0,0,0, 0,0,0,1};
    }
    void Clear() { bones_.clear(); }

private:
    std::vector<BonePose> bones_;
};

} // namespace bighero
