#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace bighero {

// Lightweight finite state machine. States are added by name, transitions
// registered from a state on a named event, and Update/HandleEvent drive it.
class StateMachine {
public:
    using UpdateFn = std::function<void(float)>;

    explicit StateMachine(const std::string& initial = "")
        : current_(initial) {}

    void AddState(const std::string& name, UpdateFn onUpdate = nullptr) {
        updates_[name] = std::move(onUpdate);
    }

    void AddTransition(const std::string& from, const std::string& event,
                       const std::string& to) {
        transitions_[from][event] = to;
    }

    void SetState(const std::string& name) { current_ = name; }

    bool HandleEvent(const std::string& event) {
        auto it = transitions_.find(current_);
        if (it == transitions_.end()) return false;
        auto e = it->second.find(event);
        if (e == it->second.end()) return false;
        current_ = e->second;
        return true;
    }

    void Update(float dt) {
        auto it = updates_.find(current_);
        if (it != updates_.end() && it->second) it->second(dt);
    }

    const std::string& Current() const { return current_; }
    std::size_t StateCount() const { return updates_.size(); }

private:
    std::string current_;
    std::map<std::string, UpdateFn> updates_;
    std::map<std::string, std::map<std::string, std::string>> transitions_;
};

} // namespace bighero
