#pragma once
#include <string>
#include <map>
#include <cstddef>
#include <cstdint>

namespace bighero {

// SaveSystem: a versioned string-keyed save slot manager. Tracks a header
// (version, scene, timestamp) for each slot plus a raw payload blob. Pure data.
class SaveSystem {
public:
    struct Slot {
        std::uint32_t version = 0;
        std::string scene;
        std::uint64_t timestamp = 0;
        std::string payload;
        bool dirty = false;
    };

    SaveSystem() {}

    void CreateSlot(const std::string& id, std::uint32_t version) {
        Slot s; s.version = version; slots_[id] = s;
    }
    bool HasSlot(const std::string& id) const { return slots_.count(id) != 0; }
    void DeleteSlot(const std::string& id) { slots_.erase(id); }
    std::size_t SlotCount() const { return slots_.size(); }

    void WritePayload(const std::string& id, const std::string& payload,
                      const std::string& scene, std::uint64_t ts) {
        auto it = slots_.find(id);
        if (it == slots_.end()) return;
        it->second.payload = payload;
        it->second.scene = scene;
        it->second.timestamp = ts;
        it->second.dirty = true;
    }

    bool ReadPayload(const std::string& id, std::string& payload, std::string& scene,
                     std::uint32_t& version) const {
        auto it = slots_.find(id);
        if (it == slots_.end()) return false;
        payload = it->second.payload;
        scene = it->second.scene;
        version = it->second.version;
        return true;
    }
    std::uint64_t Timestamp(const std::string& id) const {
        auto it = slots_.find(id);
        return it == slots_.end() ? 0 : it->second.timestamp;
    }

    void MarkClean(const std::string& id) {
        auto it = slots_.find(id);
        if (it != slots_.end()) it->second.dirty = false;
    }
    bool IsDirty(const std::string& id) const {
        auto it = slots_.find(id);
        return it == slots_.end() ? false : it->second.dirty;
    }

    void ClearAll() { slots_.clear(); }

private:
    std::map<std::string, Slot> slots_;
};

} // namespace bighero
