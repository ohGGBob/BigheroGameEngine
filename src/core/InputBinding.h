#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// InputBinding: maps a logical action name to one or more physical input
// sources (key code / axis name / mouse button), with a modifier mask.
// Pure data container consumed by the input system.
class InputBinding {
public:
    enum class Device { Unknown, Keyboard, Mouse, Gamepad };

    struct Entry {
        std::string source;      // e.g. "KeyW", "MouseLeft", "AxisX"
        Device device;
        bool isAxis;
    };

    InputBinding() {}
    explicit InputBinding(const std::string& action) : action_(action) {}

    void SetAction(const std::string& a) { action_ = a; }
    const std::string& Action() const { return action_; }

    void Bind(const std::string& source, Device device, bool isAxis = false) {
        entries_.push_back({source, device, isAxis});
    }
    void Reset() { entries_.clear(); }
    std::size_t EntryCount() const { return entries_.size(); }
    const Entry& EntryAt(std::size_t i) const { return entries_[i]; }

    // Does this binding include the given physical source?
    bool Matches(const std::string& source) const {
        for (auto& e : entries_) if (e.source == source) return true;
        return false;
    }
    // Count of entries matching a particular device.
    std::size_t CountForDevice(Device d) const {
        std::size_t n = 0;
        for (auto& e : entries_) if (e.device == d) ++n;
        return n;
    }

    void SetPositive(bool p) { positive_ = p; }
    bool Positive() const { return positive_; }
    void SetModifier(const std::string& m) { modifier_ = m; }
    const std::string& Modifier() const { return modifier_; }

private:
    std::string action_;
    std::vector<Entry> entries_;
    bool positive_ = true;
    std::string modifier_;
};

} // namespace bighero
