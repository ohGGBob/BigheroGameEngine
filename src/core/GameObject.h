#pragma once
#include <string>
#include <cstdint>

namespace bighero {

// GameObject: a lightweight named scene object identity container. Carries a
// stable id, transform (position/parent via external hierarchy) placeholder,
// active flag, and a tag. Purely data-oriented; no rendering backend.
class GameObject {
public:
    explicit GameObject(std::uint64_t id = 0, const std::string& name = "")
        : id_(id), name_(name) {}

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }
    std::uint64_t Id() const { return id_; }
    void SetId(std::uint64_t id) { id_ = id; }

    void SetActive(bool a) { active_ = a; }
    bool Active() const { return active_; }
    void SetTag(const std::string& t) { tag_ = t; }
    const std::string& Tag() const { return tag_; }

    void SetLayer(int layer) { layer_ = layer; }
    int Layer() const { return layer_; }

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }
    void Translate(float dx, float dy, float dz) { px_+=dx; py_+=dy; pz_+=dz; }

    void SetScale(float sx, float sy, float sz) { sx_=sx; sy_=sy; sz_=sz; }
    void Scale(float& sx, float& sy, float& sz) const { sx=sx_; sy=sy_; sz=sz_; }

    // Placeholder attachable component pointer (owner-managed).
    void SetComponent(void* comp) { component_ = comp; }
    void* Component() const { return component_; }

    bool IsStatic() const { return isStatic_; }
    void SetStatic(bool s) { isStatic_ = s; }

private:
    std::uint64_t id_;
    std::string name_;
    std::string tag_;
    float px_=0, py_=0, pz_=0;
    float sx_=1, sy_=1, sz_=1;
    bool active_ = true;
    bool isStatic_ = false;
    int layer_ = 0;
    void* component_ = nullptr;
};

} // namespace bighero
