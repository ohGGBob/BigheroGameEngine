#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// TypeInfo: a lightweight runtime type descriptor holding a stable type id and
// a human-readable name. Used by reflection/type-registry helpers.
// Self-contained, std-lib only.
class TypeInfo {
public:
    TypeInfo() = default;
    TypeInfo(uint64_t id, const char* name, size_t size, bool isTriviallyCopyable = false)
        : id_(id), name_(name ? name : ""), size_(size), triviallyCopyable_(isTriviallyCopyable) {}

    uint64_t Id() const { return id_; }
    const char* Name() const { return name_.c_str(); }
    size_t Size() const { return size_; }
    bool IsTriviallyCopyable() const { return triviallyCopyable_; }

    bool IsValid() const { return id_ != 0; }
    bool operator==(const TypeInfo& o) const { return id_ == o.id_; }
    bool operator!=(const TypeInfo& o) const { return id_ != o.id_; }

    void SetName(const char* n) { name_ = n ? n : ""; }
    void SetSize(size_t s) { size_ = s; }

private:
    uint64_t id_ = 0;
    std::string name_;
    size_t size_ = 0;
    bool triviallyCopyable_ = false;
};

} // namespace bighero
