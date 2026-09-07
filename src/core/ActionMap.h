#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bighero {

// Maps an action name to a set of input bindings, and offers simple querying.
class ActionMap {
public:
    enum Modifier : uint32_t {
        Shift = 1u << 0,
        Ctrl  = 1u << 1,
        Alt   = 1u << 2,
    };

    struct Binding {
        uint32_t key = 0;   // KeyCode value
        uint32_t modifiers = 0;
        bool pressed = false;
    };

    void AddBinding(const char* action, const Binding& b) {
        Bindings& binds = Find(action);
        binds.push_back(b);
    }

    bool IsActionPressed(const char* action, uint32_t heldKey, uint32_t heldMods) const {
        for (const auto& entry : actions_) {
            if (entry.first != action) continue;
            for (const auto& b : entry.second) {
                bool keyMatch = (b.key == 0) || (b.key == heldKey);
                bool modMatch = (b.modifiers & heldMods) == b.modifiers;
                if (keyMatch && modMatch) return b.pressed;
            }
        }
        return false;
    }

    // Return the first pressed key assigned to the action, or 0 if none held.
    uint32_t FirstHeldKey(const char* action, uint32_t heldMods) const {
        for (const auto& entry : actions_) {
            if (entry.first != action) continue;
            for (const auto& b : entry.second)
                if (b.pressed && (b.modifiers & heldMods) == b.modifiers) return b.key;
        }
        return 0;
    }

    void Clear() { actions_.clear(); }

private:
    using Bindings = std::vector<Binding>;
    using Entry = std::pair<std::string, Bindings>;
    Bindings& Find(const char* action) {
        for (auto& e : actions_) if (e.first == action) return e.second;
        actions_.push_back({std::string(action), Bindings()});
        return actions_.back().second;
    }
    std::vector<Entry> actions_;
};

} // namespace bighero
