#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// MaterialProperty: a named scalar/vector/texture property used by material
// systems. Supports float, int, bool, vec2/3/4, and color variants.
// Self-contained, std-lib only.
class MaterialProperty {
public:
    enum class Type : uint8_t {
        Float = 0, Int = 1, Bool = 2,
        Vec2 = 3, Vec3 = 4, Vec4 = 5, Color = 6, Texture = 7
    };

    MaterialProperty() = default;
    explicit MaterialProperty(std::string name, Type t = Type::Float)
        : name_(std::move(name)), type_(t) {}

    const std::string& Name() const { return name_; }
    void SetName(std::string n) { name_ = std::move(n); }
    Type GetType() const { return type_; }

    void SetFloat(float v) { type_ = Type::Float; f0_ = v; f1_=f2_=f3_=0; }
    void SetInt(int v) { type_ = Type::Int; i0_ = v; }
    void SetBool(bool v) { type_ = Type::Bool; b0_ = v; }
    void SetVec2(float x, float y) { type_ = Type::Vec2; f0_=x; f1_=y; f2_=f3_=0; }
    void SetVec3(float x, float y, float z) { type_ = Type::Vec3; f0_=x; f1_=y; f2_=z; f3_=0; }
    void SetVec4(float x, float y, float z, float w) { type_ = Type::Vec4; f0_=x; f1_=y; f2_=z; f3_=w; }
    void SetColor(float r, float g, float b, float a = 1.0f) {
        type_ = Type::Color; f0_=r; f1_=g; f2_=b; f3_=a;
    }
    void SetTexture(uint32_t handle) { type_ = Type::Texture; tex_ = handle; }

    float GetFloat() const { return f0_; }
    int GetInt() const { return i0_; }
    bool GetBool() const { return b0_; }
    float X() const { return f0_; }
    float Y() const { return f1_; }
    float Z() const { return f2_; }
    float W() const { return f3_; }
    uint32_t Texture() const { return tex_; }

    static const char* TypeName(Type t) {
        switch (t) {
            case Type::Float: return "Float";
            case Type::Int: return "Int";
            case Type::Bool: return "Bool";
            case Type::Vec2: return "Vec2";
            case Type::Vec3: return "Vec3";
            case Type::Vec4: return "Vec4";
            case Type::Color: return "Color";
            case Type::Texture: return "Texture";
        }
        return "Unknown";
    }

private:
    std::string name_;
    Type type_ = Type::Float;
    float f0_=0, f1_=0, f2_=0, f3_=0;
    int i0_ = 0;
    bool b0_ = false;
    uint32_t tex_ = 0;
};

} // namespace bighero
