#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// A lightweight entity handle: an opaque id plus a generation counter to
// detect stale references. Building block for an ECS-style entity store.
class Entity {
public:
    Entity() {}
    Entity(std::uint32_t index, std::uint32_t generation)
        : index_(index), generation_(generation) {}

    void Set(std::uint32_t index, std::uint32_t generation) {
        index_ = index; generation_ = generation;
    }
    std::uint32_t Index() const { return index_; }
    std::uint32_t Generation() const { return generation_; }

    void Invalidate() { index_ = kInvalid; generation_ = 0; }
    bool IsValid() const { return index_ != kInvalid; }
    static std::uint32_t InvalidIndex() { return kInvalid; }

    bool operator==(const Entity& o) const {
        return index_ == o.index_ && generation_ == o.generation_;
    }
    bool operator!=(const Entity& o) const { return !(*this == o); }

    // Compact hash for use in containers.
    std::uint64_t Hash() const {
        return ((std::uint64_t)generation_ << 32) | index_;
    }

private:
    static const std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t index_ = kInvalid;
    std::uint32_t generation_ = 0;
};

} // namespace bighero
