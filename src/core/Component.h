#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Base component: a tagged data holder that components in an ECS inherit
// from. Provides type id, name, and enable flag.
class Component {
public:
    Component() {}
    virtual ~Component() = default;

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

    // Runtime type id (unique per derived class, assigned by the store).
    void SetComponentTypeId(std::uint32_t tid) { typeId_ = tid; }
    std::uint32_t ComponentTypeId() const { return typeId_; }

    bool IsActive() const { return enabled_; }

private:
    bool enabled_ = true;
    std::uint32_t typeId_ = 0;
    std::string name_;
};

} // namespace bighero
