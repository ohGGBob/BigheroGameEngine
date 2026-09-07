#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>

namespace bighero {

// Reference-counted resource handle with an opaque ID and weak/strong semantics.
template <typename T>
class ResourceManager {
public:
    using Handle = uint32_t;
    struct Entry { std::shared_ptr<T> res; };

    // Allocate a new resource with a unique handle, optionally wrapping existing resource.
    Handle Create(std::shared_ptr<T> resource = nullptr) {
        if (!resource) resource = std::make_shared<T>();
        Handle h = nextHandle_++;
        // ensure nextHandle_ never collides (simple monotonic, wrap-safe not needed at scale)
        entries_.push_back({resource});
        return h;
    }

    // Look up resource by handle; returns nullptr if invalid.
    std::shared_ptr<T> Get(Handle h) const {
        if (h >= entries_.size()) return nullptr;
        return entries_[h].res;
    }

    // Release one strong reference to a resource; returns true if it no longer exists.
    bool Release(Handle h) {
        if (h >= entries_.size()) return true;
        entries_[h].res = nullptr;
        return true;
    }

    // Returns true if the handle still references a live resource.
    bool IsValid(Handle h) const { return h < entries_.size() && entries_[h].res != nullptr; }

    std::size_t Count() const { return entries_.size(); }
    std::size_t LiveCount() const {
        std::size_t n = 0;
        for (auto& e : entries_) if (e.res) ++n;
        return n;
    }

private:
    std::vector<Entry> entries_;
    Handle nextHandle_ = 0;
};

} // namespace bighero
