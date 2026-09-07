#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Skeleton: a set of joints (bones) with parent relationships and local transforms.
class Skeleton {
public:
    struct Joint {
        std::string name;
        int parent = -1;
        // local transform (translation + quaternion-lite via euler + scale)
        float tx = 0, ty = 0, tz = 0;
        float rx = 0, ry = 0, rz = 0;   // euler radians
        float sx = 1, sy = 1, sz = 1;

        Joint() {}
        Joint(const char* n, int p) : name(n), parent(p) {}
    };

    int AddJoint(const char* name, int parent = -1) {
        Joint j(name, parent);
        if (parent < 0 || parent >= (int)joints_.size()) j.parent = -1;
        int id = (int)joints_.size();
        joints_.push_back(j);
        children_.resize(joints_.size());
        if (j.parent >= 0) children_[j.parent].push_back(id);
        return id;
    }

    int FindJoint(const char* name) const {
        for (size_t i = 0; i < joints_.size(); ++i)
            if (joints_[i].name == name) return (int)i;
        return -1;
    }

    int Root() const { return joints_.empty() ? -1 : 0; /* assume first is root */ }
    int Size() const { return (int)joints_.size(); }
    bool Empty() const { return joints_.empty(); }

    Joint& Get(int id) { return joints_[id]; }
    const Joint& Get(int id) const { return joints_[id]; }
    int Parent(int id) const { return joints_[id].parent; }

    // Set local transform of a joint.
    void SetLocalTransform(int id, float tx, float ty, float tz,
                           float rx = 0, float ry = 0, float rz = 0,
                           float sx = 1, float sy = 1, float sz = 1) {
        Joint& j = joints_[id];
        j.tx=tx; j.ty=ty; j.tz=tz; j.rx=rx; j.ry=ry; j.rz=rz;
        j.sx=sx; j.sy=sy; j.sz=sz;
    }

private:
    std::vector<Joint> joints_;
    std::vector<std::vector<int>> children_;
};

} // namespace bighero
