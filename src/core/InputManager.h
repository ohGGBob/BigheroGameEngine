#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstddef>

namespace bighero {

// Input manager: track pressed/held/released keyboard and mouse states by
// named action bindings. Pure CPU-side state, frame-advance driven.
class InputManager {
public:
    enum class MouseBtn { Left, Right, Middle };

    void SetDigitalAction(const std::string& name, bool down) {
        auto& st = actions_[name];
        if (down) {
            if (!st.down) { st.pressed = true; st.released = false; }
            st.down = true;
        } else {
            if (st.down) { st.pressed = false; st.released = true; }
            st.down = false;
        }
    }
    bool IsActionDown(const std::string& name) const {
        auto it = actions_.find(name); return it != actions_.end() && it->second.down;
    }
    bool IsActionPressed(const std::string& name) const {
        auto it = actions_.find(name); return it != actions_.end() && it->second.pressed;
    }
    bool IsActionReleased(const std::string& name) const {
        auto it = actions_.find(name); return it != actions_.end() && it->second.released;
    }

    // Mouse position (NDC or pixels, backend-defined).
    void SetMousePosition(float x, float y) { mx_ = x; my_ = y; }
    void MousePosition(float& x, float& y) const { x = mx_; y = my_; }
    void SetMouseButton(MouseBtn b, bool down) { mouseDown_[(int)b] = down; }
    bool IsMouseDown(MouseBtn b) const { return mouseDown_[(int)b]; }

    // Clear per-frame transient flags (call at end of frame).
    void EndFrame() {
        for (auto& kv : actions_) { kv.second.pressed = false; kv.second.released = false; }
    }

    std::size_t ActionCount() const { return actions_.size(); }

private:
    struct State { bool down = false, pressed = false, released = false; };
    std::map<std::string, State> actions_;
    float mx_ = 0, my_ = 0;
    bool mouseDown_[3] = {false, false, false};
};

} // namespace bighero
