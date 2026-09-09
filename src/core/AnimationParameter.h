#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// AnimationParameter: a typed animation parameter binding (float/int/bool/
// vector/color) used to drive animator properties from curves. Self-contained.
class AnimationParameter {
public:
    enum class Type { Float, Int, Bool, Vector3, Color };

    AnimationParameter() = default;
    AnimationParameter(Type type, uint32_t nameHash, int property = 0)
        : type_(type), nameHash_(nameHash), property_(property) {}

    void SetType(Type t) { type_ = t; }
    Type GetType() const { return type_; }
    void SetNameHash(uint32_t h) { nameHash_ = h; }
    uint32_t GetNameHash() const { return nameHash_; }
    void SetProperty(int p) { property_ = p; }
    int GetProperty() const { return property_; }

    // Setters store a typed value into the appropriate slot.
    void SetFloat(float v) { if (type_ == Type::Float) f_ = v; }
    void SetInt(int v) { if (type_ == Type::Int) i_ = v; }
    void SetBool(bool v) { if (type_ == Type::Bool) b_ = v; }
    void SetVector3(float x, float y, float z) {
        if (type_ == Type::Vector3) { vx_ = x; vy_ = y; vz_ = z; }
    }
    void SetColor(float r, float g, float b, float a) {
        if (type_ == Type::Color) { cr_ = r; cg_ = g; cb_ = b; ca_ = a; }
    }

    float GetFloat() const { return f_; }
    int GetInt() const { return i_; }
    bool GetBool() const { return b_; }
    void GetVector3(float& x, float& y, float& z) const { x = vx_; y = vy_; z = vz_; }
    void GetColor(float& r, float& g, float& b, float& a) const { r = cr_; g = cg_; b = cb_; a = ca_; }

private:
    Type type_ = Type::Float;
    uint32_t nameHash_ = 0;
    int property_ = 0;
    float f_ = 0; int i_ = 0; bool b_ = false;
    float vx_ = 0, vy_ = 0, vz_ = 0;
    float cr_ = 1, cg_ = 1, cb_ = 1, ca_ = 1;
};

} // namespace bighero
