#pragma once
#include <vector>
#include <utility>
#include <cstddef>

namespace bighero {

// Simple named property bag that can hold int/float/bool/string values and
// remember the order they were set (unlike a map), useful for UI/serialization.
class PropertyMap {
public:
    enum class Type { None, Int, Float, Bool, String, Vec2 };

    struct Entry {
        std::string name;
        Type type = Type::None;
        int i = 0;
        float f = 0;
        bool b = false;
        std::string s;
        float x = 0, y = 0;
    };

    void SetInt(const std::string& name, int v) { auto& e = Get(name); e.type=Type::Int; e.i=v; }
    void SetFloat(const std::string& name, float v) { auto& e = Get(name); e.type=Type::Float; e.f=v; }
    void SetBool(const std::string& name, bool v) { auto& e = Get(name); e.type=Type::Bool; e.b=v; }
    void SetString(const std::string& name, const std::string& v) { auto& e = Get(name); e.type=Type::String; e.s=v; }
    void SetVec2(const std::string& name, float x, float y) { auto& e = Get(name); e.type=Type::Vec2; e.x=x; e.y=y; }

    int GetInt(const std::string& name, int def = 0) const {
        const Entry* e = Find(name); return (e && e->type==Type::Int) ? e->i : def;
    }
    float GetFloat(const std::string& name, float def = 0) const {
        const Entry* e = Find(name); return (e && e->type==Type::Float) ? e->f : def;
    }
    bool GetBool(const std::string& name, bool def = false) const {
        const Entry* e = Find(name); return (e && e->type==Type::Bool) ? e->b : def;
    }
    std::string GetString(const std::string& name, const std::string& def = "") const {
        const Entry* e = Find(name); return (e && e->type==Type::String) ? e->s : def;
    }

    bool Has(const std::string& name) const { return Find(name) != nullptr; }
    std::size_t Size() const { return entries_.size(); }
    bool Empty() const { return entries_.empty(); }
    const Entry& At(std::size_t idx) const { return entries_[idx]; }
    void Clear() { entries_.clear(); }

private:
    Entry& Get(const std::string& name) {
        for (auto& e : entries_) if (e.name == name) return e;
        entries_.push_back({name, Type::None, 0, 0.0f, false, std::string(), 0.0f, 0.0f});
        return entries_.back();
    }
    const Entry* Find(const std::string& name) const {
        for (auto& e : entries_) if (e.name == name) return &e;
        return nullptr;
    }
    std::vector<Entry> entries_;
};

} // namespace bighero
