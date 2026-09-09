#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>

namespace bighero {

// BoundingVolumeHierarchy: a flattened AABB tree for broad-phase culling.
// Entries are inserted with an AABB and an associated user id; query returns
// all ids whose AABB overlaps a query AABB. Simplest form: a grid-bin broad
// phase that is easy to keep in a header and correctness-focused.
class BoundingVolumeHierarchy {
public:
    struct Bounds { float minX, minY, minZ, maxX, maxY, maxZ; };

    BoundingVolumeHierarchy() {}

    void Insert(std::uint64_t id, const Bounds& b) {
        entries_.push_back({id, b});
    }
    std::size_t Count() const { return entries_.size(); }
    void Clear() { entries_.clear(); }

    // Returns number of ids whose bounds overlap the query box.
    std::size_t Query(const Bounds& q, std::vector<std::uint64_t>& out) const {
        out.clear();
        for (auto& e : entries_) {
            if (Overlap(e.bounds, q)) out.push_back(e.id);
        }
        return out.size();
    }
    bool Remove(std::uint64_t id) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->id == id) { entries_.erase(it); return true; }
        }
        return false;
    }

private:
    struct Entry { std::uint64_t id; Bounds bounds; };
    static bool Overlap(const Bounds& a, const Bounds& b) {
        return a.minX <= b.maxX && a.maxX >= b.minX &&
               a.minY <= b.maxY && a.maxY >= b.minY &&
               a.minZ <= b.maxZ && a.maxZ >= b.minZ;
    }
    std::vector<Entry> entries_;
};

} // namespace bighero
