#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// AssetHandle: a lightweight, copyable handle identifying a loaded asset. Holds
// an id, type tag and a cached display name. Self-contained, std-lib only.
class AssetHandle {
public:
    AssetHandle() = default;
    AssetHandle(uint64_t id, const char* name) : id_(id), name_(name ? name : "") {}
    explicit AssetHandle(uint64_t id) : id_(id) {}

    uint64_t Id() const { return id_; }
    void SetId(uint64_t id) { id_ = id; }

    const std::string& Name() const { return name_; }
    void SetName(const char* name) { name_ = name ? name : ""; }

    // Type tag (e.g. "Texture","Mesh","Audio").
    void SetType(const char* type) { type_ = type ? type : ""; }
    const std::string& Type() const { return type_; }

    bool IsValid() const { return id_ != 0; }
    void Invalidate() { id_ = 0; }
    bool operator==(const AssetHandle& o) const { return id_ == o.id_; }
    bool operator!=(const AssetHandle& o) const { return id_ != o.id_; }

    uint64_t Hash() const { return id_ ? id_ : 1; }

private:
    uint64_t id_ = 0;
    std::string name_;
    std::string type_;
};

} // namespace bighero
