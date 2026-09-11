#pragma once
#include <cstdint>

namespace bighero {

// PoseBoneMap: maps skeleton bone indices to (parent, weight) pairs for pose
// evaluation. Self-contained / std-lib only.
class PoseBoneMap {
public:
    PoseBoneMap() = default;
    PoseBoneMap(int boneIndex, int parentIndex) : bone_(boneIndex), parent_(parentIndex) {}

    void SetBone(int b) { bone_ = b; }
    int Bone() const { return bone_; }
    void SetParent(int p) { parent_ = p; }
    int Parent() const { return parent_; }

    void SetLocalRotation(float w, float x, float y, float z) { lw_=w; lx_=x; ly_=y; lz_=z; }
    float Lw() const { return lw_; } float Lx() const { return lx_; }
    float Ly() const { return ly_; } float Lz() const { return lz_; }

    bool IsRoot() const { return parent_ < 0; }
    bool IsValid() const { return bone_ >= 0; }
    void Reset() { bone_=-1; parent_=-1; lw_=1; lx_=ly_=lz_=0; }

private:
    int bone_ = -1, parent_ = -1;
    float lw_=1, lx_=0, ly_=0, lz_=0;
};

} // namespace bighero
