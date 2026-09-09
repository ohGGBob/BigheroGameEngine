#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// SceneNode: a node in a scene graph hierarchy. Carries a stable id, name,
// local transform, parent/child links, active flag. Purely data-oriented.
class SceneNode {
public:
    explicit SceneNode(std::uint64_t id = 0, const std::string& name = "")
        : id_(id), name_(name) {}

    void SetId(std::uint64_t id) { id_ = id; }
    std::uint64_t Id() const { return id_; }
    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

    void SetParent(SceneNode* p) { parent_ = p; }
    SceneNode* Parent() const { return parent_; }
    void AddChild(SceneNode* c) { children_.push_back(c); }
    std::size_t ChildCount() const { return children_.size(); }
    SceneNode* Child(std::size_t i) const { return i < children_.size() ? children_[i] : nullptr; }

    void SetLocalPosition(float x, float y, float z) { lx_=x; ly_=y; lz_=z; }
    void LocalPosition(float& x, float& y, float& z) const { x=lx_; y=ly_; z=lz_; }
    void SetLocalRotation(float rx, float ry, float rz) { rx_=rx; ry_=ry; rz_=rz; }
    void SetLocalScale(float sx, float sy, float sz) { sx_=sx; sy_=sy; sz_=sz; }

    void SetActive(bool a) { active_ = a; }
    bool Active() const { return active_; }
    void SetTag(const std::string& t) { tag_ = t; }
    const std::string& Tag() const { return tag_; }

    // Effective activation: own active flag AND parent chain.
    bool ActiveInHierarchy() const {
        const SceneNode* n = this;
        while (n) { if (!n->active_) return false; n = n->parent_; }
        return true;
    }

private:
    std::uint64_t id_;
    std::string name_;
    std::string tag_;
    SceneNode* parent_ = nullptr;
    std::vector<SceneNode*> children_;
    float lx_=0, ly_=0, lz_=0;
    float rx_=0, ry_=0, rz_=0;
    float sx_=1, sy_=1, sz_=1;
    bool active_ = true;
};

} // namespace bighero
